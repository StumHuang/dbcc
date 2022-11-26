#include "mex.h"
#include "matrix.h"
#include <assert.h>
#include <stdint.h>
#include "mpc.h"
#include "util.h"
#include "can.h"
#include "parse.h"
#include "2c.h"
#include "2xml.h"
#include "2csv.h"
#include "2bsm.h"
#include "2json.h"
#include "options.h"

mxArray * msg2struct(can_msg_t *msg)
{
	assert(msg);
	signal_t *multiplexor = NULL;

	const char *signalnames[] = {"Name","Start","Length","Endianess","Scaling","Offset","Minimum", "Maximum", "Signed", "Units", "Multiplexed","Floating","Comment","Receiving"};
	mwSize dims[2] = {1, msg->signal_count};
	mxArray *signals = mxCreateStructArray(2, dims, 14, signalnames);

	for(size_t i = 0; i < msg->signal_count; i++) {
		
		char sv[64];
		const char *multi = "N/A";
		signal_t *sig = msg->sigs[i];
		if(sig->is_multiplexor) {
			if(multiplexor) {
				error("multiple multiplexor values detected (only one per CAN msg is allowed) for %s", msg->name);
			}
			multi = "multiplexor";
		}
		if(sig->is_multiplexed) {
			sprintf(sv, "%d", sig->switchval);
			multi = sv;
		}

		bool have_units = false;
		const char *units = sig->units;
		for(size_t j = 0; units[j]; j++)
			if(!isspace(units[j]))
				have_units = true;
		if(!have_units)
			units = "none";

		const char *floating = "no";
		if (sig->is_floating) {
			assert(sig->sigval == 1 || sig->sigval == 2);
			floating = (sig->sigval == 1) ? "single" : "double";
		}


		mxSetField(signals, i,"Name", mxCreateString(sig->name));
		mxSetField(signals, i,"Start",mxCreateDoubleScalar(sig->start_bit));
		mxSetField(signals, i,"Length", mxCreateDoubleScalar(sig->bit_length));
		mxSetField(signals, i,"Endianess", mxCreateString(sig->endianess == endianess_motorola_e ? "motorola" : "intel"));
		mxSetField(signals, i,"Scaling", mxCreateDoubleScalar(sig->scaling));
		mxSetField(signals, i,"Offset", mxCreateDoubleScalar(sig->offset));
		mxSetField(signals, i,"Minimum", mxCreateDoubleScalar(sig->minimum));
		mxSetField(signals, i,"Maximum", mxCreateDoubleScalar(sig->maximum));
		mxSetField(signals, i,"Signed", mxCreateString(sig->is_signed ? "true" : "false"));
		mxSetField(signals, i,"Units", mxCreateString(units));
		mxSetField(signals, i,"Multiplexed", mxCreateString(multi));
		mxSetField(signals, i,"Floating", mxCreateString(floating));
		mxSetField(signals, i,"Comment", mxCreateString(sig->comment));
        
        
        const char *ReceiverNames[] = {"Receiver"};
        mwSize dims[2] = {1, sig->ecu_count};
        mxArray *Receivers = mxCreateStructArray(2, dims, 1, ReceiverNames);
        
        for (int k = 0;k<sig->ecu_count;k++)
            mxSetField(Receivers, k,"Receiver", mxCreateString(sig->ecus[k]));
        
		mxSetField(signals, i,"Receiving", Receivers);

	}
	return signals;
}

mxArray * messages2struct(dbc_t *dbc)
{
	assert(dbc);

	mwSize dims[2] = {1, dbc->message_count};
	const char *messagenames[] = {"Name","SendECU","ID","DLC","Signals","Comment"};
	mxArray *messages = mxCreateStructArray(2, dims, 6, messagenames);

	for (size_t i = 0; i < dbc->message_count; i++)
	{
		mxSetField(messages, i,"Name", mxCreateString(dbc->messages[i]->name));
		mxSetField(messages, i,"SendECU", mxCreateString(dbc->messages[i]->ecu));
		mxSetField(messages, i,"ID", mxCreateDoubleScalar(dbc->messages[i]->id));
		mxSetField(messages, i,"DLC", mxCreateDoubleScalar(dbc->messages[i]->dlc));
		mxSetField(messages, i,"Signals", msg2struct(dbc->messages[i]));
		mxSetField(messages, i,"Comment", mxCreateString(dbc->messages[i]->comment));
	}
	return messages;
}

void mexFunction(int nlhs, mxArray *plhs[], int nrhs,const mxArray *prhs[])
{
    
    char *st = mxArrayToString(prhs[0]);

    mpc_ast_t *ast = parse_dbc_file_by_name(st);
		if (!ast) 
        {
			warning("could not parse file '%s'", st);
		}
    
        
		dbc_t *dbc = ast2dbc(ast);
        
        assert(dbc);
        
        plhs[0] =  messages2struct(dbc);
        dbc_delete(dbc);
		mpc_ast_delete(ast);
        
}
