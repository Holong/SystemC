#ifndef __DSP_H__
#define __DSP_H__

#include <systemc.h>
#include <tlm.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>
#include <tlm_utils/peq_with_cb_and_phase.h>
#include <list>
#include <iostream>


#include "list.h"

using std::list;
using sc_core::sc_time;
using sc_dt::uint64;
using std::cout;
using std::endl;

/* For extension phase */
DECLARE_EXTENDED_PHASE(internal_ph);

class DSP : public sc_module
{
	/*
	 * DSP operations
	 */
	enum command { ADD = 1, SUB };

	/*
	 * DSP states
	 *
	 * READY : DSP doesn't anything. The dsp state will be changed to RUN from READY
	 * when dsp cmd register value is changed.
	 *
	 * RUN : DSP is calculating something.
	 *
	 * COMPLETE : DSP operation is completed.
	 *
	 */
	enum state { READY = 0, RUN, COMPLETE };

private:
	tlm_utils::peq_with_cb_and_phase<DSP> peq_for_data;
	tlm_utils::peq_with_cb_and_phase<DSP> peq_for_int;

	tlm::tlm_command int_command;
	unsigned int operand1, operand2, dsp_status, result;
	list<tlm::tlm_generic_payload*> wait_queue;
	bool response_in_progress;

public:
	/*
	 * The DSP has two socket. The one is interrupt_socket, another is data_socket.
	 * Interrupt_socket models a complete interrupt which occurs at COMPLETE state transition.
	 * Data_socket models read/write commands.
	 */
	tlm_utils::simple_initiator_socket<DSP> interrupt_socket;
	tlm_utils::simple_target_socket<DSP> data_socket;

	tlm::tlm_generic_payload* packet;

	SC_CTOR(DSP) : peq_for_data(this, &DSP::peq_for_data_cb), peq_for_int(this, &DSP::peq_for_int_cb),
		       int_command(tlm::TLM_IGNORE_COMMAND), operand1(0), operand2(0), dsp_status(READY), result(0),  
		       interrupt_socket("INT_SOCKET"), data_socket("DATA_SOCKET") 
	{
		interrupt_socket.register_nb_transport_bw(this, &DSP::nb_transport_bw);

		data_socket.register_b_transport(this, &DSP::b_transport);
		data_socket.register_nb_transport_fw(this, &DSP::nb_transport_fw);
	}

	/*
	 * Implements a non-blocking forward method.
	 * Phase arguments have to has tlm::BEGIN_REQ or tlm::END_RESP.
	 */
	virtual tlm::tlm_sync_enum nb_transport_fw(tlm::tlm_generic_payload& packet, tlm::tlm_phase& phase, sc_time& delay)
	{
		peq_for_data.notify(packet, phase, delay);
		return tlm::TLM_ACCEPTED;
	}

	/*
	 * Implements a blocking forward method. Not yet.
	 */
	virtual void b_transport(tlm::tlm_generic_payload& packet, sc_time& delay)
	{

	}

	/*
	 * Implements a non-blocking backword method. Not yet.
	 */
	virtual tlm::tlm_sync_enum nb_transport_bw(tlm::tlm_generic_payload& packet, tlm::tlm_phase& phase, sc_time& delay)
	{
		return tlm::TLM_ACCEPTED;
	}

	/*
	 * Implements a payload event queue call back method for interrupt. Not yet.
	 */
	void peq_for_int_cb(tlm::tlm_generic_payload& packet, const tlm::tlm_phase& phase)
	{

	}

	/*
	 * Implements a payload event queue call back method for read/write cmd.
	 */
	void peq_for_data_cb(tlm::tlm_generic_payload& packet, const tlm::tlm_phase& phase)
	{
		switch(phase)
		{
		case tlm::BEGIN_REQ:
			packet.acquire();
			send_end_req(packet);
			break;
		case tlm::END_RESP:
			end_transaction(packet);
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
					wait_queue.push_back(&packet);
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
		sc_time delay = sc_time(1, SC_NS);

		data_socket->nb_transport_bw(packet, phase, delay);

		tlm::tlm_phase int_phase = internal_ph;
		delay = delay + sc_time(0, SC_NS);
		peq_for_data.notify(packet, int_phase, delay);
	}

	void send_response(tlm::tlm_generic_payload& packet)
	{
		tlm::tlm_sync_enum status;
		tlm::tlm_phase phase;
		sc_time delay;

		response_in_progress = true;
		phase = tlm::BEGIN_RESP;
		delay = sc_time(50, SC_NS);
		status = data_socket->nb_transport_bw(packet, phase, delay);

		if(status == tlm::TLM_UPDATED)
		{
			peq_for_data.notify(packet, phase, delay);
		}
		else if(status == tlm::TLM_COMPLETED)
		{
			end_transaction(packet);
		}
	}

	void end_transaction(tlm::tlm_generic_payload& packet)
	{
		packet.release();
		response_in_progress = false;

		if(!wait_queue.empty())
		{
			list<tlm::tlm_generic_payload*>::iterator it;
			it = wait_queue.begin();
			tlm::tlm_generic_payload* ptr = *it;
			send_response(*ptr);
			wait_queue.erase(it);
		}
	}
};


#endif	
