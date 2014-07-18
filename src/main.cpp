#define SC_INCLUDE_DYNAMIC_PROCESS

#include <systemc.h>
#include <tlm.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

class Initiator : sc_module
{
private:
	int data;
	tlm::tlm_dmi dmi_data;
	bool dmi_ptr_valid;
	
public:
	tlm_utils::simple_initiator_socket<Initiator> socket;
	SC_HAS_PROCESS(Initiator);
	Initiator(sc_module_name name) : sc_module(name), socket("socket")
	{
		socket.register_invalidate_direct_mem_ptr(this, &Initiator::invalidate_direct_mem_ptr);
		SC_THREAD(thread);
	}

	virtual void invalidate_direct_mem_ptr(sc_dt::uint64 start_range, sc_dt::uint64 end_range)
	{
		dmi_ptr_valid = false;
	}

	void thread(void)
	{
		tlm::tlm_generic_payload* trans = new tlm::tlm_generic_payload;
		tlm::tlm_command cmd = tlm::TLM_READ_COMMAND;

		for(int i = 0; i < 1024; i += 4)
		{
			sc_time delay = sc_time(10, SC_NS);
			if(dmi_ptr_valid && sc_dt::uint64(i) >= dmi_data.get_start_address() && sc_dt::uint64(i) <= dmi_data.get_end_address())
			{
				if(cmd == tlm::TLM_READ_COMMAND)
				{
					assert(dmi_data.is_read_allowed());
					memcpy(&data, dmi_data.get_dmi_ptr() + i, 4);
					wait(dmi_data.get_read_latency());
				}
				else if(cmd == tlm::TLM_WRITE_COMMAND)
				{
					assert(dmi_data.is_write_allowed());
					memcpy(dmi_data.get_dmi_ptr() + i, &data, 4);
					wait(dmi_data.get_write_latency());
				}
				cout << "DMI   = [CMD: " << (cmd ? 'W' : 'R') << ", ADR: " << i
				     << "], data = " << data << " at time " << sc_time_stamp() << endl;
			}
			else
			{
				trans->set_command(cmd);
				trans->set_address(i);
				trans->set_data_ptr(reinterpret_cast<unsigned char*>(&data));
				trans->set_data_length(4);
				trans->set_streaming_width(4);
				trans->set_byte_enable_ptr(0);
				trans->set_dmi_allowed(false);
				trans->set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);

				socket->b_transport(*trans, delay);

				if(trans->is_response_error())
					SC_REPORT_ERROR("TLM-2", "Response error from b_transport");

				if(trans->is_dmi_allowed())
				{
					dmi_data.init();
					dmi_ptr_valid = socket->get_direct_mem_ptr(*trans, dmi_data);
				}

				cout << "trans = { " << (cmd ? 'W' : 'R') << ", " << i
					<< " }, data = " << data << " at time " << sc_time_stamp()
					<< " delay = " << delay << endl;
			}
		}
	}
};

template <unsigned int SIZE = 256>
class Memory : sc_module
{
private:
	int mem[SIZE/4];
	const sc_time DMI_LATENCY;

public:
	tlm_utils::simple_target_socket<Memory> socket;
	SC_HAS_PROCESS(Memory);
	Memory(sc_module_name name) : sc_module(name), DMI_LATENCY(1, SC_NS), socket("socket")
	{
		socket.register_b_transport(this, &Memory::b_transport);
		socket.register_get_direct_mem_ptr(this, &Memory::get_direct_mem_ptr);
		
		for(unsigned int i = 0; i < SIZE/4; i++)
			mem[i] = i*4;
	}

	virtual void b_transport(tlm::tlm_generic_payload& trans, sc_time& delay)
	{
		tlm::tlm_command cmd = trans.get_command();
		sc_dt::uint64 adr = trans.get_address();
		unsigned char* ptr = trans.get_data_ptr();
		unsigned int len = trans.get_data_length();
		unsigned char* byt = trans.get_byte_enable_ptr();
		unsigned int wid = trans.get_streaming_width();

		if(adr >= sc_dt::uint64(SIZE))
		{
			trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
			return;
		}
		if(byt != 0)
		{
			trans.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
			return;
		}
		if(len > 4 || wid < len)
		{
			trans.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
			return;
		}

		if(cmd == tlm::TLM_READ_COMMAND)
			memcpy(ptr, &mem[adr/4], len);
		else if(cmd == tlm::TLM_WRITE_COMMAND)
			memcpy(&mem[adr/4], ptr, len);

		wait(delay);
		delay = SC_ZERO_TIME;
		trans.set_dmi_allowed(true);
		
		trans.set_response_status(tlm::TLM_OK_RESPONSE);
	}

	virtual bool get_direct_mem_ptr(tlm::tlm_generic_payload& trans, tlm::tlm_dmi& dmi_data)
	{
		dmi_data.allow_read_write();
		dmi_data.set_dmi_ptr(reinterpret_cast<unsigned char*>(&mem[0]));
		dmi_data.set_start_address(0);
		dmi_data.set_end_address(SIZE - 1);
		dmi_data.set_read_latency(DMI_LATENCY);
		dmi_data.set_write_latency(DMI_LATENCY);

		return true;
	}
};

template<unsigned int N_TARGETS, unsigned int SIZE=256>
class Router : sc_module
{
public:
	tlm_utils::simple_target_socket<Router> target_socket;
	tlm_utils::simple_initiator_socket_tagged<Router>* initiator_sockets[N_TARGETS];

	SC_HAS_PROCESS(Router);
	Router(sc_module_name name) : sc_module(name), target_socket("target_socket")
	{
		target_socket.register_b_transport(this, &Router::b_transport);
		target_socket.register_get_direct_mem_ptr(this, &Router::get_direct_mem_ptr);

		for(unsigned int i = 0; i < N_TARGETS; i++)
		{
			char txt[20];
			sprintf(txt, "socket_%d", i);
			initiator_sockets[i] = new tlm_utils::simple_initiator_socket_tagged<Router>(txt);
			initiator_sockets[i]->register_invalidate_direct_mem_ptr(this, &Router::invalidate_direct_mem_ptr, i);
		}
	}

	virtual void b_transport(tlm::tlm_generic_payload& trans, sc_time& delay)
	{
		sc_dt::uint64 address = trans.get_address();
		
		unsigned int target_nr = address / SIZE;

		trans.set_address(address % SIZE);
		
		(*initiator_sockets[target_nr])->b_transport(trans, delay);
		trans.set_address(target_nr * SIZE + address % SIZE);
	}

	virtual bool get_direct_mem_ptr(tlm::tlm_generic_payload& trans, tlm::tlm_dmi& dmi_data)
	{
		sc_dt::uint64 address = trans.get_address();
		unsigned int target_nr = address / SIZE;
		trans.set_address(address % SIZE);
		bool status = (*initiator_sockets[target_nr])->get_direct_mem_ptr(trans, dmi_data);

		dmi_data.set_start_address(target_nr * SIZE + dmi_data.get_start_address());
		dmi_data.set_end_address(target_nr * SIZE + dmi_data.get_end_address());
		unsigned char* temp_adr = dmi_data.get_dmi_ptr();
		temp_adr -= target_nr*SIZE;
		dmi_data.set_dmi_ptr(temp_adr);

		return status;
	}

	virtual void invalidate_direct_mem_ptr(int id, sc_dt::uint64 start_range, sc_dt::uint64 end_range)
	{
		sc_dt::uint64 bw_start_range = id * SIZE + start_range;
		sc_dt::uint64 bw_end_range = id* SIZE + end_range;
		target_socket->invalidate_direct_mem_ptr(bw_start_range, bw_end_range);
	}
};

class Top : sc_module
{
private:
	Initiator* initiator;
	Memory<>* memory[4];
	Router<4>* router;

public:
	Top(sc_module_name name) : sc_module(name)
	{
		initiator = new Initiator("initiator");
		router = new Router<4>("router");
		for(int i = 0; i < 4; i++)
		{
			char txt[20];
			sprintf(txt, "memory_%d", i);
			memory[i] = new Memory<>(txt);
		}

		initiator->socket.bind(router->target_socket);
		for(int i = 0; i < 4; i++)
			router->initiator_sockets[i]->bind(memory[i]->socket);
	}
};

int sc_main(int argc, char* argv[])
{
	Top top("top");

	sc_start();

	return 0;
}
