#ifndef __DSP_H__
#define __DSP_H__

#include <systemc.h>
#include <tlm.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>
#include <tlm_utils/peq_with_cb_and_phase.h>

#include "list.h"

using sc_core::sc_time;
using sc_dt::uint64;
using std::cout;
using std::endl;

DECLARE_EXTENDED_PHASE(internal_ph);

class DSP : public sc_module
{
	enum command { ADD = 1, SUB };
	enum state { READY = 0, RUN, COMPLETE };
	struct element_packet {
		list_head* list;
		tlm::tlm_generic_payload* ptr;
	};

private:
	tlm_utils::peq_with_cb_and_phase<DSP> peq_for_data;
	tlm_utils::peq_with_cb_and_phase<DSP> peq_for_int;

	tlm::tlm_command int_command;
	unsigned int operand1, operand2, dsp_status, result;
	struct list_head response_wait_list;
	bool response_in_progress;

public:
	tlm_utils::simple_initiator_socket<DSP> interrupt_socket;
	tlm_utils::simple_target_socket<DSP> data_socket;

	tlm::tlm_generic_payload* packet;

	SC_CTOR(DSP) : peq_for_data(this, &DSP::peq_for_data_cb), peq_for_int(this, &DSP::peq_for_int_cb),
		       int_command(tlm::TLM_IGNORE_COMMAND), operand1(0), operand2(0), dsp_status(READY), result(0),  
		       interrupt_socket("INT_SOCKET"), data_socket("DATA_SOCKET") 
	{
		INIT_LIST_HEAD(&response_wait_list);
		interrupt_socket.register_nb_transport_bw(this, &DSP::nb_transport_bw);

		data_socket.register_b_transport(this, &DSP::b_transport);
		data_socket.register_nb_transport_fw(this, &DSP::nb_transport_fw);
	}

	virtual tlm::tlm_sync_enum nb_transport_fw(tlm::tlm_generic_payload& packet, tlm::tlm_phase& phase, sc_time& delay)
	{
		peq_for_data.notify(packet, phase, delay);
		return tlm::TLM_ACCEPTED;
	}

	virtual void b_transport(tlm::tlm_generic_payload& packet, sc_time& delay)
	{

	}

	virtual tlm::tlm_sync_enum nb_transport_bw(tlm::tlm_generic_payload& packet, tlm::tlm_phase& phase, sc_time& delay)
	{
		return tlm::TLM_ACCEPTED;
	}

	void peq_for_int_cb(tlm::tlm_generic_payload& packet, const tlm::tlm_phase& phase)
	{

	}

	void peq_for_data_cb(tlm::tlm_generic_payload& packet, const tlm::tlm_phase& phase)
	{
		switch(phase)
		{
		case tlm::BEGIN_REQ:
			packet.acquire();
			send_end_req(packet);
			break;
		case tlm::END_RESP:
			packet.release();
			response_in_progress = false;

			if(!list_empty(&response_wait_list))
			{
				tlm::tlm_phase phase = internal_ph;
				struct element_packet* ptr;
				ptr = list_first_entry(&response_wait_list, element_packet, list);
				list_del(ptr->list);
				peq_for_data.notify(*(ptr->ptr), phase, sc_time(50, SC_NS));
				free(ptr);
			}
			break;
		default:
			if(phase == internal_ph)
			{
				tlm::tlm_command cmd = packet.get_command();
				uint64 adr = packet.get_address();
				unsigned char* ptr = packet.get_data_ptr();
				unsigned int len = packet.get_data_length();

				if(cmd == tlm::TLM_READ_COMMAND)
				{
					switch(adr)
					{
						case 0:
							memcpy(ptr, &int_command, len);
							packet.set_response_status(tlm::TLM_OK_RESPONSE);
							break;
						case 4:
							memcpy(ptr, &operand1, len);
							packet.set_response_status(tlm::TLM_OK_RESPONSE);
							break;
						case 8:
							memcpy(ptr, &operand2, len);
							packet.set_response_status(tlm::TLM_OK_RESPONSE);
							break;
						case 12:
							memcpy(ptr, &dsp_status, len);
							packet.set_response_status(tlm::TLM_OK_RESPONSE);
							break;
						case 16:
							memcpy(ptr, &result, len);
							packet.set_response_status(tlm::TLM_OK_RESPONSE);
							break;
						default:
							packet.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
					}
				}
				else if(cmd == tlm::TLM_WRITE_COMMAND)
				{
					switch(adr)
					{
						case 0:
							memcpy(&int_command, ptr, len);
							packet.set_response_status(tlm::TLM_OK_RESPONSE);
							break;
						case 4:
							memcpy(&operand1, ptr, len);
							packet.set_response_status(tlm::TLM_OK_RESPONSE);
							break;
						case 8:
							memcpy(&operand2, ptr, len);
							packet.set_response_status(tlm::TLM_OK_RESPONSE);
							break;
						case 12:
							memcpy(&dsp_status, ptr, len);
							packet.set_response_status(tlm::TLM_OK_RESPONSE);
							break;
						case 16:
							memcpy(&result, ptr, len);
							packet.set_response_status(tlm::TLM_OK_RESPONSE);
							break;
						default:
							packet.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
					}
				}

				if(response_in_progress)
				{
					struct element_packet* temp = (struct element_packet*)malloc(sizeof(struct element_packet));
					temp->ptr = &packet;
					list_add_tail(temp->list, &response_wait_list);
				}
				else
				{
					send_response(packet);
				}
			}
		}
	}

	void send_end_req(tlm::tlm_generic_payload& packet)
	{
		tlm::tlm_phase phase = tlm::END_REQ;
		tlm::tlm_phase int_phase = internal_ph;
		sc_time delay = sc_time(1, SC_NS);

		data_socket->nb_transport_bw(packet, phase, delay);

		delay = delay + sc_time(50, SC_NS);
		peq_for_data.notify(packet, int_phase, delay);
	}

	void send_response(tlm::tlm_generic_payload& packet)
	{
		tlm::tlm_sync_enum status;
		tlm::tlm_phase phase;
		sc_time delay;

		response_in_progress = true;
		phase = tlm::BEGIN_RESP;
		delay = SC_ZERO_TIME;
		status = data_socket->nb_transport_bw(packet, phase, delay);

		if(status == tlm::TLM_UPDATED)
		{
			peq_for_data.notify(packet, phase, delay);
		}
		else if(status == tlm::TLM_COMPLETED)
		{
			packet.release();
			response_in_progress = false;
		}
	}
};


#endif	
