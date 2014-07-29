#include <systemc.h>
#include "list.h"
#include "dsp.h"
#include "core.h"

class Top : public sc_module
{
public:
	Core* core;
	DSP* dsp;

	SC_CTOR(Top)
	{
		core = new Core("core");
		dsp = new DSP("dsp");

		core->socket.bind(dsp->data_socket);
		dsp->interrupt_socket.bind(core->interrupt);
	}
};

int sc_main(int argc, char* argv[])
{
	Top top("Top");

	sc_start();

	return 0;
}
