#ifndef __CORE_H__
#define __CORE_H__

#include <systemc.h>
#include <tlm.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/peq_with_cb_and_phase.h>

#include "mem_manager.h"

class Core: public sc_module
{
public:
	// TLM-2 socket, defaults to 32-bits wide, base protocol
	tlm_utils::simple_initiator_socket<Core> socket;
	tlm_utils::simple_target_socket<Core> interrupt;
	tlm::tlm_generic_payload* request_in_progress;
	tlm_utils::peq_with_cb_and_phase<Core> m_peq;

	mm   m_mm;
	int  data[16];
	sc_event end_request_event;

	SC_CTOR(Core) : socket("socket"), interrupt("INT"), request_in_progress(0), m_peq(this, &Core::peq_cb)
	{
		for(int i = 0; i < 16; i++)
			data[i] = i;

		socket.register_nb_transport_bw(this, &Core::nb_transport_bw);

		SC_THREAD(thread_process);
	}

	void thread_process()
	{
		tlm::tlm_generic_payload* trans;
		tlm::tlm_phase phase;
		sc_time delay;

		for (int i = 0; i < 20; i=i+4)
		{
			int adr = i;
			tlm::tlm_command cmd = tlm::TLM_WRITE_COMMAND;

			// Grab a new transaction from the memory manager
			trans = m_mm.allocate();
			trans->acquire();

			// Set all attributes except byte_enable_length and extensions (unused)
			trans->set_command( cmd );
			trans->set_address( adr );
			trans->set_data_ptr( reinterpret_cast<unsigned char*>(&data[i/4]) );
			trans->set_data_length( 4 );
			trans->set_streaming_width( 4 ); // = data_length to indicate no streaming
			trans->set_byte_enable_ptr( 0 ); // 0 indicates unused
			trans->set_dmi_allowed( false ); // Mandatory initial value
			trans->set_response_status( tlm::TLM_INCOMPLETE_RESPONSE ); // Mandatory initial value

			// Core must honor BEGIN_REQ/END_REQ exclusion rule
			if (request_in_progress)
				wait(end_request_event);
			request_in_progress = trans;
			phase = tlm::BEGIN_REQ;

			// Timing annotation models processing time of initiator prior to call
			delay = sc_time(1, SC_NS);

			// Non-blocking transport call on the forward path
			tlm::tlm_sync_enum status;
			status = socket->nb_transport_fw( *trans, phase, delay );

			// Check value returned from nb_transport_fw
			if (status == tlm::TLM_UPDATED)
			{
				// The timing annotation must be honored
				m_peq.notify( *trans, phase, delay );
			}
			else if (status == tlm::TLM_COMPLETED)
			{
				// The completion of the transaction necessarily ends the BEGIN_REQ phase
				request_in_progress = 0;

				// The target has terminated the transaction
				check_transaction( *trans );
			}
			wait( sc_time(10, SC_NS) );
		}

		for (int i = 0; i < 20; i=i+4)
		{
			int adr = i;
			tlm::tlm_command cmd = tlm::TLM_READ_COMMAND;

			// Grab a new transaction from the memory manager
			trans = m_mm.allocate();
			trans->acquire();

			// Set all attributes except byte_enable_length and extensions (unused)
			trans->set_command( cmd );
			trans->set_address( adr );
			trans->set_data_ptr( reinterpret_cast<unsigned char*>(&data[i/4]) );
			trans->set_data_length( 4 );
			trans->set_streaming_width( 4 ); // = data_length to indicate no streaming
			trans->set_byte_enable_ptr( 0 ); // 0 indicates unused
			trans->set_dmi_allowed( false ); // Mandatory initial value
			trans->set_response_status( tlm::TLM_INCOMPLETE_RESPONSE ); // Mandatory initial value

			// Core must honor BEGIN_REQ/END_REQ exclusion rule
			if (request_in_progress)
				wait(end_request_event);
			request_in_progress = trans;
			phase = tlm::BEGIN_REQ;

			// Timing annotation models processing time of initiator prior to call
			delay = sc_time(1, SC_NS);

			// Non-blocking transport call on the forward path
			tlm::tlm_sync_enum status;
			status = socket->nb_transport_fw( *trans, phase, delay );

			// Check value returned from nb_transport_fw
			if (status == tlm::TLM_UPDATED)
			{
				// The timing annotation must be honored
				m_peq.notify( *trans, phase, delay );
			}
			else if (status == tlm::TLM_COMPLETED)
			{
				// The completion of the transaction necessarily ends the BEGIN_REQ phase
				request_in_progress = 0;

				// The target has terminated the transaction
				check_transaction( *trans );
			}
			wait( sc_time(10, SC_NS) );
		}
	}

	// TLM-2 backward non-blocking transport method
	virtual tlm::tlm_sync_enum nb_transport_bw( tlm::tlm_generic_payload& trans,
			tlm::tlm_phase& phase, sc_time& delay )
	{
		m_peq.notify( trans, phase, delay );
		return tlm::TLM_ACCEPTED;
	}

	// Payload event queue callback to handle transactions from target
	// Transaction could have arrived through return path or backward path
	void peq_cb(tlm::tlm_generic_payload& trans, const tlm::tlm_phase& phase)
	{
#ifdef DEBUG
		if (phase == tlm::END_REQ)
			cout << hex << trans.get_address() << " END_REQ at " << sc_time_stamp() << endl;
		else if (phase == tlm::BEGIN_RESP)
			cout << hex << trans.get_address() << " BEGIN_RESP at " << sc_time_stamp() << endl;
#endif

		if (phase == tlm::END_REQ || (&trans == request_in_progress && phase == tlm::BEGIN_RESP))
		{
			// The end of the BEGIN_REQ phase
			request_in_progress = 0;
			end_request_event.notify();
		}
		else if (phase == tlm::BEGIN_REQ || phase == tlm::END_RESP)
			SC_REPORT_FATAL("TLM-2", "Illegal transaction phase received by initiator");

		if (phase == tlm::BEGIN_RESP)
		{
			check_transaction( trans );

			// Send final phase transition to target
			tlm::tlm_phase fw_phase = tlm::END_RESP;
			sc_time delay = sc_time(1, SC_NS);
			socket->nb_transport_fw( trans, fw_phase, delay );
			// Ignore return value
		}
	}

	// Called on receiving BEGIN_RESP or TLM_COMPLETED
	void check_transaction(tlm::tlm_generic_payload& trans)
	{
		if ( trans.is_response_error() )
		{
			char txt[100];
			sprintf(txt, "Transaction returned with error, response status = %s",
					trans.get_response_string().c_str());
			SC_REPORT_ERROR("TLM-2", txt);
		}

		tlm::tlm_command cmd = trans.get_command();
		sc_dt::uint64    adr = trans.get_address();
		int*             ptr = reinterpret_cast<int*>( trans.get_data_ptr() );

		cout<< hex << adr << " check, cmd=" << (cmd ? 'W' : 'R')
			<< ", data=" << hex << *ptr << " at time " << sc_time_stamp() << endl;

		// Allow the memory manager to free the transaction object
		trans.release();
	}

};

#endif
