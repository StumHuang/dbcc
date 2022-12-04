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

mxArray *attribute2struct(attribute_values *attributes)
{
    const char *attributenames[] = {"Name","Value","Values","AttributeType"};
	mwSize dims[2] = {1, attributes->attribute_value_count};
	mxArray *attribute = mxCreateStructArray(2, dims, 4, attributenames);
	
	for(size_t i = 0 ; i < attributes->attribute_value_count ; i++)
	{
		char *attribute_name = attributes->attribute[i]->definition->name;
		int  attribute_type  = attributes->attribute[i]->definition->att_type;

		mxSetField(attribute, i,"Name", mxCreateString(attribute_name));
		
		switch(attribute_type)
		{
			case INT_:
				mxSetField(attribute, i,"AttributeType", mxCreateString("INT"));
				mxSetField(attribute, i,"Value", mxCreateDoubleScalar(attributes->attribute[i]->value.signed_integer));

				mwSize INTn[2] = {1,1};
				const char *MaxMinINTName[] = {"Max","Min"};
				mxArray *MaxMinINT = mxCreateStructArray(2, INTn, 2, MaxMinINTName);

				mxSetField(MaxMinINT, 0,"Min", mxCreateDoubleScalar(attributes->attribute[i]->definition->value.INT_.min));
				mxSetField(MaxMinINT, 0,"Max", mxCreateDoubleScalar(attributes->attribute[i]->definition->value.INT_.max));

				mxSetField(attribute, i,"Values", MaxMinINT);

				break;

			case HEX_:
				mxSetField(attribute, i,"AttributeType", mxCreateString("HEX"));
				mxSetField(attribute, i,"Value", mxCreateDoubleScalar(attributes->attribute[i]->value.signed_integer));

				mwSize HEXn[2] = {1,1};
				const char *MaxMinHEXName[] = {"Max","Min"};
				mxArray *MaxMinHEX = mxCreateStructArray(2, HEXn, 2, MaxMinHEXName);

				mxSetField(MaxMinHEX, 0,"Min", mxCreateDoubleScalar(attributes->attribute[i]->definition->value.HEX_.min));
				mxSetField(MaxMinHEX, 0,"Max", mxCreateDoubleScalar(attributes->attribute[i]->definition->value.HEX_.max));

				mxSetField(attribute, i,"Values", MaxMinHEX);
				break;

			case FLOAT_:
				mxSetField(attribute, i,"AttributeType", mxCreateString("FLOAT"));
				mxSetField(attribute, i,"Value", mxCreateDoubleScalar(attributes->attribute[i]->value.FLOAT));

				mwSize FLOATn[2] = {1,1};
				const char *MaxMinFLOATName[] = {"Max","Min"};
				mxArray *MaxMinFLOAT = mxCreateStructArray(2, FLOATn, 2, MaxMinFLOATName);

				mxSetField(MaxMinFLOAT, 0,"Min", mxCreateDoubleScalar(attributes->attribute[i]->definition->value.FLOAT_.min));
				mxSetField(MaxMinFLOAT, 0,"Max", mxCreateDoubleScalar(attributes->attribute[i]->definition->value.FLOAT_.max));

				mxSetField(attribute, i,"Values", MaxMinFLOAT);
				break;

			case STRING_:
				mxSetField(attribute, i,"AttributeType", mxCreateString("STRING"));
				mxSetField(attribute, i,"Values", mxCreateString(attributes->attribute[i]->definition->value.STRING));
				mxSetField(attribute, i,"Value", mxCreateString(attributes->attribute[i]->value.char_string));
				break;

			case ENUM_:
				mxSetField(attribute, i,"AttributeType", mxCreateString("ENUM"));
				mxSetField(attribute, i,"Value", mxCreateString(attributes->attribute[i]->value.char_string));

				mxArray *ENUM_list = mxCreateCellMatrix(1, attributes->attribute[i]->definition->value.ENUM_.count);
				for(size_t j = 0 ; j < attributes->attribute[i]->definition->value.ENUM_.count ; j++)
				{
					mxSetCell(ENUM_list,j,mxCreateString(attributes->attribute[i]->definition->value.ENUM_.ENUM_list[j]));
				}
				mxSetField(attribute, i,"Values", ENUM_list);
				break;
		}
		
	}
	return attribute;
}

mxArray *val2struct(val_list_t *val_list)
{
	const char *valnames[] = {"Name","ID","Count","val_list_items"};
	mwSize dims[2] = {1, 1};
	mxArray *val = mxCreateStructArray(2, dims, 4, valnames);

	mxSetField(val, 0,"Name", mxCreateString(val_list->name));
	mxSetField(val, 0,"ID", mxCreateDoubleScalar(val_list->id));
	mxSetField(val, 0,"Count", mxCreateDoubleScalar(val_list->val_list_item_count));

	const char *itemnames[] = {"Name","Value"};
	mwSize dims1[2] = {1, val_list->val_list_item_count};
	mxArray *items = mxCreateStructArray(2, dims1, 2, itemnames);

	for(size_t i = 0 ; i < val_list->val_list_item_count ; i++)
	{
		mxSetField(items, i,"Name", mxCreateString(val_list->val_list_items[i]->name));
		mxSetField(items, i,"Value", mxCreateDoubleScalar(val_list->val_list_items[i]->value));
	}

	mxSetField(val, 0,"val_list_items", items);

	return val;
}

mxArray *signal2struct(can_msg_t *msg)
{
	assert(msg);
	signal_t *multiplexor = NULL;

	const char *signalnames[] = {"Name","Start","Length","Endianess","Scaling","Offset","Minimum", "Maximum", "Signed", "Units", "Multiplexed","Floating","Comment","Receiving","Attribute","Val"};
	mwSize dims[2] = {1, msg->signal_count};
	mxArray *signals = mxCreateStructArray(2, dims, 16, signalnames);

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
        
        /* create Receive ECUs in Cell*/

		mxArray *Receiver = mxCreateCellMatrix(1, sig->ecu_count);
		for(size_t j = 0 ; j < sig->ecu_count ; j++)
		{
			mxSetCell(Receiver,j,mxCreateString(sig->ecus[j]));
		}
		mxSetField(signals, i,"Receiving", Receiver);
		mxSetField(signals, i,"Attribute", attribute2struct(sig->attributes));

		if(sig->val_list)
		{
			mxSetField(signals, i,"Val", val2struct(sig->val_list));
		}
		
		
		 
	}
	return signals;
}

mxArray *messages2struct(dbc_t *dbc)
{
	assert(dbc);

	mwSize dims[2] = {1, dbc->message_count};
	const char *messagenames[] = {"Name","SendECU","ID","DLC","Signals","Comment","Attribute"};
	mxArray *messages = mxCreateStructArray(2, dims, 7, messagenames);

	for (size_t i = 0; i < dbc->message_count; i++)
	{
		mxSetField(messages, i,"Name", mxCreateString(dbc->messages[i]->name));
		mxSetField(messages, i,"SendECU", mxCreateString(dbc->messages[i]->ecu));
		mxSetField(messages, i,"ID", mxCreateDoubleScalar(dbc->messages[i]->id));
		mxSetField(messages, i,"DLC", mxCreateDoubleScalar(dbc->messages[i]->dlc));
		mxSetField(messages, i,"Signals", signal2struct(dbc->messages[i]));
		mxSetField(messages, i,"Comment", mxCreateString(dbc->messages[i]->comment));
		mxSetField(messages, i,"Attribute", attribute2struct(dbc->messages[i]->attributes));
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
