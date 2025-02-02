/**@note Error checking is not done in this file, or if it is done it should be
 * done with assertions, the parser does the validation and processing of the
 * input, if a returned object is not checked it is because it *must* exist (or
 * there is a bug in the grammar).
 * @note most values will be converted to doubles and then to integers, which
 * is fine for 32 bit values, but fails for large integers. */
#include "can.h"
#include "util.h"
#include <assert.h>
#include <stdlib.h>
#include <inttypes.h>
#include <math.h>

static signal_t *signal_new(void)
{
	return allocate(sizeof(signal_t));
}

static void signal_delete(signal_t *signal)
{
	if (!signal)
		return;
	for(unsigned int i = 0; i < signal->ecu_count; i++)
		free(signal->ecus[i]);

	for(int i = 0; i < signal->attributes->attribute_value_count; i++)
		free(signal->attributes->attribute[i]);
	
	free(signal->name);
	free(signal->ecus);
	free(signal->units);
	free(signal->comment);
	free(signal);
}

static can_msg_t *can_msg_new(void)
{
	return allocate(sizeof(can_msg_t));
}

static void can_msg_delete(can_msg_t *msg)
{
	if (!msg)
		return;
	for(unsigned int i = 0; i < msg->signal_count; i++)
		signal_delete(msg->sigs[i]);

	for(int i = 0; i < msg->attributes->attribute_value_count; i++)
		free(msg->attributes->attribute[i]);
	free(msg->sigs);
	free(msg->name);
	free(msg->ecu);
	free(msg->comment);
	free(msg);
}

static void val_delete(val_list_t *val)
{
	if (!val)
		return;
	for(size_t i = 0; i < val->val_list_item_count; i++) {
		free(val->val_list_items[i]->name);
		free(val->val_list_items[i]);
	}
	free(val);
}

static void y_mx_c(mpc_ast_t *ast, signal_t *sig)
{
	assert(ast && sig);
	mpc_ast_t *scalar = ast->children[1];
	mpc_ast_t *offset = ast->children[3];
	int r = sscanf(scalar->contents, "%lf", &sig->scaling);
	assert(r == 1);
	r = sscanf(offset->contents, "%lf", &sig->offset);
	assert(r == 1);
}

static void range(mpc_ast_t *ast, signal_t *sig)
{
	assert(ast && sig);
	mpc_ast_t *min = ast->children[1];
	mpc_ast_t *max = ast->children[3];
	int r = sscanf(min->contents, "%lf", &sig->minimum);
	assert(r == 1);
	r = sscanf(max->contents, "%lf", &sig->maximum);
	assert(r == 1);
}

static void units(mpc_ast_t *ast, signal_t *sig)
{
	assert(ast && sig);
	mpc_ast_t *unit = mpc_ast_get_child(ast, "regex");
	sig->units = duplicate(unit->contents);
}

static void ecus(mpc_ast_t *ast, signal_t *sig)
{
	assert(ast && sig);

	mpc_ast_t *node = mpc_ast_get_child(ast, "nodes|>");
	
	if (node != NULL)
	{
		char **nodes = allocate(sizeof(*nodes) * (node->children_num+1));
		for(int i = 0; i >= 0;) 
		{
			i = mpc_ast_get_index_lb(node, "node|ident|regex", i);
			if (i >=0)
			{
				nodes[sig->ecu_count++] = duplicate(node->children[i]->contents);
				i++;
			}
		}
		sig->ecus = nodes;
	}
	else
	{
		char **nodes = allocate(sizeof(*nodes));
		mpc_ast_t *node  = mpc_ast_get_child(ast, "nodes|node|ident|regex");
		nodes[sig->ecu_count++] = duplicate(node->contents);
		sig->ecus = nodes;
	}	
}

static int sigval(mpc_ast_t *top, unsigned id, const char *signal)
{
	assert(top);
	assert(signal);
	for(int i = 0; i >= 0;) {
		i = mpc_ast_get_index_lb(top, "sigval|>", i);
		if (i >= 0) {
			mpc_ast_t *sv = mpc_ast_get_child_lb(top, "sigval|>", i);
			mpc_ast_t *name   = mpc_ast_get_child(sv, "name|ident|regex");
			mpc_ast_t *svid = mpc_ast_get_child(sv,   "id|integer|regex");
			assert(name);
			assert(svid);
			unsigned svidd = 0;
			sscanf(svid->contents, "%u", &svidd);
			if (id == svidd && !strcmp(signal, name->contents)) {
				unsigned typed = 0;
				mpc_ast_t *type = mpc_ast_get_child(sv, "sigtype|integer|regex");
				sscanf(type->contents, "%u", &typed);
				debug("floating -> %s:%u:%u\n", name->contents, id, typed);
				return typed;
			}
			i++;
		}
	}
	return -1;
}

static signal_t *ast2signal(mpc_ast_t *top, mpc_ast_t *ast, unsigned can_id)
{
	int r;
	assert(ast);
	signal_t *sig = signal_new();
	mpc_ast_t *name   = mpc_ast_get_child(ast, "name|ident|regex");
	mpc_ast_t *start  = mpc_ast_get_child(ast, "startbit|integer|regex");
	mpc_ast_t *length = mpc_ast_get_child(ast, "length|regex");
	mpc_ast_t *endianess = mpc_ast_get_child(ast, "endianess|char");
	mpc_ast_t *sign   = mpc_ast_get_child(ast, "sign|char");
	sig->name = duplicate(name->contents);
	sig->val_list = NULL;
	sig->attributes    = allocate(sizeof(attribute_values));
	r = sscanf(start->contents, "%u", &sig->start_bit);
	/* BUG: Minor bug, an error should be returned here instead */
	assert(r == 1 && sig->start_bit <= 64);
	r = sscanf(length->contents, "%u", &sig->bit_length);
	assert(r == 1 && sig->bit_length <= 64);
	char endchar = endianess->contents[0];
	assert(endchar == '0' || endchar == '1');
	sig->endianess = endchar == '0' ?
		endianess_motorola_e :
		endianess_intel_e ;
	char signchar = sign->contents[0];
	assert(signchar == '+' || signchar == '-');
	sig->is_signed = signchar == '-';

	y_mx_c(mpc_ast_get_child(ast, "y_mx_c|>"), sig);
	range(mpc_ast_get_child(ast, "range|>"), sig);
	units(mpc_ast_get_child(ast, "unit|string|>"), sig);
	ecus(ast, sig);
	/*nodes(mpc_ast_get_child(ast, "nodes|node|ident|regex|>"), sig);*/

	/* process multiplexed values, if present */
	mpc_ast_t *multiplex = mpc_ast_get_child(ast, "multiplexor|>");
	if (multiplex) {
		sig->is_multiplexed = true;
		sig->switchval = atol(multiplex->children[1]->contents);
	}

	if (mpc_ast_get_child(ast, "multiplexor|char")) {
		assert(!sig->is_multiplexed);
		sig->is_multiplexor = true;
	}

	sig->sigval = sigval(top, can_id, sig->name);
	if (sig->sigval == 1 || sig->sigval == 2)
		sig->is_floating = true;

	debug("\tname => %s; start %u length %u %s %s %s %s",
			sig->name, sig->start_bit, sig->bit_length, sig->units,
			sig->endianess ? "intel" : "motorola",
			sig->is_signed ? "signed " : "unsigned",sig->ecus[0]);
	return sig;
}

static val_list_t *ast2val(mpc_ast_t *top, mpc_ast_t *ast)
{
	assert(top);
	assert(ast);
	val_list_t *val = allocate(sizeof(val_list_t));

	mpc_ast_t *id   = mpc_ast_get_child(ast, "id|integer|regex");
	int r = sscanf(id->contents,  "%u",  &val->id);
	assert(r == 1);

	mpc_ast_t *name = mpc_ast_get_child(ast, "name|ident|regex");
	val->name = duplicate(name->contents);

	val_list_item_t **items = allocate(sizeof(*items) * (ast->children_num+1));
	int j = 0;
	for(int i = 0; i >= 0;) {
		i = mpc_ast_get_index_lb(ast, "val_item|>", i);
		if (i >= 0) {
			val_list_item_t *item = allocate(sizeof(val_list_item_t));
			mpc_ast_t *val_item_ast = mpc_ast_get_child_lb(ast, "val_item|>", i);

			mpc_ast_t *val_item_index = mpc_ast_get_child(val_item_ast, "integer|regex");
			int r = sscanf(val_item_index->contents,  "%u",  &item->value);
			assert(r == 1);

			mpc_ast_t *val_item_name = mpc_ast_get_child(val_item_ast, "string|>");
			val_item_name = mpc_ast_get_child_lb(val_item_name, "regex", 1);
			item->name = duplicate(val_item_name->contents);
			items[j++] = item;
			i++;
		}
	}

	val->val_list_item_count = j;
	val->val_list_items = items;

	// sort the value items by value
	if (val->val_list_item_count) {
		bool bFlip = false;
		do {
			bFlip = false;
			for (size_t i = 0; i < val->val_list_item_count - 1; i++) {
				if (val->val_list_items[i]->value > val->val_list_items[i + 1]->value) {
					val_list_item_t *tmp = val->val_list_items[i];
					val->val_list_items[i] = val->val_list_items[i + 1];
					val->val_list_items[i + 1] = tmp;
					bFlip = true;
				}
			}
		} while (bFlip);
	}

	return val;
}

static can_msg_t *ast2msg(mpc_ast_t *top, mpc_ast_t *ast, dbc_t *dbc)
{
	assert(top);
	assert(ast);
	can_msg_t *c = can_msg_new();
	mpc_ast_t *name = mpc_ast_get_child(ast, "name|ident|regex");
	mpc_ast_t *ecu  = mpc_ast_get_child(ast, "ecu|ident|regex");
	mpc_ast_t *dlc  = mpc_ast_get_child(ast, "dlc|integer|regex");
	mpc_ast_t *id   = mpc_ast_get_child(ast, "id|integer|regex");
	c->name = duplicate(name->contents);
	c->ecu  = duplicate(ecu->contents);
	int r = sscanf(dlc->contents, "%u", &c->dlc);
	assert(r == 1);
	r = sscanf(id->contents,  "%lu", &c->id);
	assert(r == 1);

	signal_t **signal_s = allocate(sizeof(*signal_s));
	size_t len = 1, j = 0;
	for(int i = 0; i >= 0;) {
		i = mpc_ast_get_index_lb(ast, "signal|>", i);
		if (i >= 0) {
			mpc_ast_t *sig_ast = mpc_ast_get_child_lb(ast, "signal|>", i);
			signal_s = reallocator(signal_s, sizeof(*signal_s)*++len);
			signal_s[j++] = ast2signal(top, sig_ast, c->id);
			i++;
		}
	}

	c->sigs = signal_s;
	c->signal_count = j;

	// assign val-s to the signals
	for (size_t i = 0; i < c->signal_count; i++) {
		for (size_t j = 0; j<dbc->val_count; j++) {
			if (dbc->vals[j]->id == c->id && strcmp(dbc->vals[j]->name, c->sigs[i]->name) == 0) {
				c->sigs[i]->val_list = dbc->vals[j];
				break;
			}
		}
	}

	if (c->signal_count > 1) { // Lets sort the signals so that their start_bit is asc (lowest number first)
		bool bFlip = false;
		do {
			bFlip = false;
			for (size_t i = 0; i < c->signal_count - 1; i++) {
				if (c->sigs[i]->start_bit > c->sigs[i + 1]->start_bit) {
					signal_t *tmp = c->sigs[i];
					c->sigs[i] = c->sigs[i + 1];
					c->sigs[i + 1] = tmp;
					bFlip = true;
				}
			}
		} while (bFlip);
	}

	c->attributes = allocate(sizeof(attribute_values));

	debug("%s id:%u dlc:%u signals:%zu ecu:%s", c->name, c->id, c->dlc, c->signal_count, c->ecu);
	return c;
}

dbc_t *dbc_new(void)
{
	return allocate(sizeof(dbc_t));
}

void dbc_delete(dbc_t *dbc)
{
	if (!dbc)
		return;
	for (size_t i = 0; i < dbc->message_count; i++)
		can_msg_delete(dbc->messages[i]);

	for (size_t i = 0; i < dbc->val_count; i++)
		val_delete(dbc->vals[i]);

	free(dbc);
}

void assign_comment_to_signal(dbc_t *dbc, const char *comment, unsigned message_id, const char * signal_name)
{
	for (size_t i = 0; i<dbc->message_count; i++) {
		if (dbc->messages[i]->id == message_id) {
			for (size_t j = 0; j<dbc->messages[i]->signal_count; j++) {
				if (strcmp(dbc->messages[i]->sigs[j]->name, signal_name) == 0) {
					dbc->messages[i]->sigs[j]->comment = duplicate(comment);
					return;
				}
			}
		}
	}
}

void assign_comment_to_message(dbc_t *dbc, const char *comment, unsigned message_id)
{
	for (size_t i = 0; i<dbc->message_count; i++) {
		if (dbc->messages[i]->id == message_id) {
			dbc->messages[i]->comment = duplicate(comment);
			return;
		}
	}
}

void SetAttributeDefaultValue(attribute_value *attribute)
{
	mpc_ast_t *value;
	switch (attribute->definition->att_type) /*   attribute_value = unsigned_integer | signed_integer | double | char_string ; attribute_value has't unsigned_integer*/
		{
			case INT_:	
				attribute->value.signed_integer = attribute->definition->inivalue.signed_integer;
				break;
			
			case HEX_:
				attribute->value.signed_integer = attribute->definition->inivalue.signed_integer;   								/* i don't konw the context is HEX*/
				break;

			case FLOAT_:
				attribute->value.FLOAT = attribute->definition->inivalue.FLOAT;
				break;

			case STRING_:
				if (attribute->definition->inivalue.char_string ==NULL) {attribute->definition->inivalue.char_string = "";}
				attribute->value.char_string = duplicate(attribute->definition->inivalue.char_string);
				break;

			case ENUM_:
				
				attribute->value.char_string = duplicate(attribute->definition->value.ENUM_.ENUM_list[attribute->definition->inivalue.unsigned_integer]);
				break;

		}

}

void assign_attribute_to_object(dbc_t *dbc,attribute_definitions *definitions)
{
	for(int i = 0 ; i < definitions->attribute_definition_count ; i++)
	{
		switch (definitions->attributes[i]->obj_type)
		{
			case BU_:
				break;

			case BO_:
				for(size_t j = 0 ; j < dbc->message_count ; j++)
				{
					if(dbc->messages[j]->attributes->attribute_value_count == 0)
					{
						dbc->messages[j]->attributes->attribute = allocate(sizeof(attribute_value));
					}
					dbc->messages[j]->attributes->attribute = reallocator(dbc->messages[j]->attributes->attribute,sizeof(attribute_value)*++dbc->messages[j]->attributes->attribute_value_count);
					dbc->messages[j]->attributes->attribute[dbc->messages[j]->attributes->attribute_value_count-1] = allocate(sizeof(attribute_definition));
					dbc->messages[j]->attributes->attribute[dbc->messages[j]->attributes->attribute_value_count-1]->definition = definitions->attributes[i];
					SetAttributeDefaultValue(dbc->messages[j]->attributes->attribute[dbc->messages[j]->attributes->attribute_value_count-1]);
				}
				break;

			case SG_:
				for(size_t j = 0 ; j < dbc->message_count ; j++)
				{
					for(size_t k = 0 ; k < dbc->messages[j]->signal_count ; k++)
					{
						if(dbc->messages[j]->sigs[k]->attributes->attribute_value_count ==0 )
						{
							dbc->messages[j]->sigs[k]->attributes->attribute = allocate(sizeof(attribute_value));
						}
						dbc->messages[j]->sigs[k]->attributes->attribute = reallocator(dbc->messages[j]->sigs[k]->attributes->attribute , sizeof(attribute_value)*++dbc->messages[j]->sigs[k]->attributes->attribute_value_count);
						dbc->messages[j]->sigs[k]->attributes->attribute[dbc->messages[j]->sigs[k]->attributes->attribute_value_count-1] = allocate(sizeof(attribute_definition));
						dbc->messages[j]->sigs[k]->attributes->attribute[dbc->messages[j]->sigs[k]->attributes->attribute_value_count-1]->definition = definitions->attributes[i];
						SetAttributeDefaultValue(dbc->messages[j]->sigs[k]->attributes->attribute[dbc->messages[j]->sigs[k]->attributes->attribute_value_count-1]);
					}
				}
				break;

			case EV_:
				break;

			default:
				break;			
		}
	}
}

void SetAttributeInit(mpc_ast_t *ast,attribute_value *attribute)
{
	mpc_ast_t *value;
	switch (attribute->definition->att_type) /*   attribute_value = unsigned_integer | signed_integer | double | char_string ; attribute_value has't unsigned_integer*/
		{
			case INT_:	
				value = mpc_ast_get_child(ast, "attribute_value|float|regex");
				sscanf(value->contents, "%d",&attribute->value.signed_integer);
				break;
			
			case HEX_:
				value = mpc_ast_get_child(ast, "attribute_value|float|regex");
				sscanf(value->contents,  "%d", &attribute->value.signed_integer);   								/* i don't konw the context is HEX*/
				break;

			case FLOAT_:
				value = mpc_ast_get_child(ast, "attribute_value|float|regex");
				sscanf(value->contents,  "%f", &attribute->value.FLOAT);
				break;

			case STRING_:
				value = mpc_ast_get_child(ast, "attribute_value|string|>");
				attribute->value.char_string = duplicate(mpc_ast_get_child(value,"regex")->contents);
				break;

			case ENUM_:
				value = mpc_ast_get_child(ast, "attribute_value|float|regex");
				int indx;
				sscanf(value->contents,  "%u", &indx);
				attribute->value.char_string = duplicate(attribute->definition->value.ENUM_.ENUM_list[indx]);
				break;

		}

}

void assign_init_attribute(mpc_ast_t *ast,dbc_t *dbc)
{
	mpc_ast_t *attribute_list = mpc_ast_get_child(ast, "attribute_values|>");
	for(int i = 0; i<attribute_list->children_num;i++)
	{
		/*get name of attribute values */
		mpc_ast_t *attribute_name = mpc_ast_get_child(attribute_list->children[i], "attribute_name|string|>");
		mpc_ast_t *name = mpc_ast_get_child(attribute_name,"regex");
		mpc_ast_t *obj_type = mpc_ast_get_child(attribute_list->children[i], "object_type|string");

		object_type OBJ_Type;
		if (obj_type==NULL)
		{
			OBJ_Type = ET_;
		}
		else if (strcmp(obj_type->contents,"BU_")==0)
		{
			OBJ_Type = BU_;
			mpc_ast_t *node = mpc_ast_get_child(attribute_list->children[i], "node|ident|regex");
			char *node_name = duplicate(node->contents);
		}
		else if (strcmp(obj_type->contents,"BO_")==0)
		{
			OBJ_Type = BO_;
			mpc_ast_t *id = mpc_ast_get_child(attribute_list->children[i], "id|integer|regex");
			unsigned long message;
			sscanf(id->contents,  "%lu", &message);
			char *att_name = duplicate(name->contents);
			for(size_t j = 0;j<dbc->message_count;j++)
			{
				if(message == dbc->messages[j]->id)
				{
					for(int k = 0;k<dbc->messages[j]->attributes->attribute_value_count;k++)
					{
						if(strcmp(att_name,dbc->messages[j]->attributes->attribute[k]->definition->name)==0)
						{
							SetAttributeInit(attribute_list->children[i],dbc->messages[j]->attributes->attribute[k]);
							break;
						}
					}
				}
			}
		}
		else if (strcmp(obj_type->contents,"SG_")==0)
		{
			OBJ_Type = SG_;
			mpc_ast_t *id = mpc_ast_get_child(attribute_list->children[i], "id|integer|regex");
			unsigned long message;
			sscanf(id->contents,  "%lu", &message);

			mpc_ast_t *signalnames = mpc_ast_get_child(attribute_list->children[i], "name|ident|regex");
			char *signal_name = duplicate(signalnames->contents);
			char *att_name = duplicate(name->contents);

			for(size_t j = 0;j<dbc->message_count;j++)
			{
				if(message == dbc->messages[j]->id)
				{
					for(size_t k = 0;k<dbc->messages[j]->signal_count;k++)
					{
						if(strcmp(signal_name,dbc->messages[j]->sigs[k]->name)==0)
						{
							for(int l =0;l<dbc->messages[j]->sigs[k]->attributes->attribute_value_count;l++)
							{
								if(strcmp(att_name,dbc->messages[j]->sigs[k]->attributes->attribute[l]->definition->name)==0)
								{
									SetAttributeInit(attribute_list->children[i],dbc->messages[j]->sigs[k]->attributes->attribute[l]);
									break;
								}
							}
						}
					}
				}
			}

		}
		else
		{
			OBJ_Type = EV_;
			mpc_ast_t *node = mpc_ast_get_child(attribute_list->children[i], "node|ident|regex");
			char *node_name = duplicate(node->contents);
		}

	}
}

void ast2attributedefinitions(mpc_ast_t *ast,dbc_t *dbc)
{
	mpc_ast_t *attributes = mpc_ast_get_child(ast, "attribute_definitions|>");

	if(attributes==NULL) {return;}

	attribute_definition **definitions = allocate(sizeof(attribute_definition)*attributes->children_num);
	for(int i = 0;i<attributes->children_num;i++)
	{
		definitions[i] = allocate(sizeof(attribute_definition));

        /*get name of attribute_definition */
		mpc_ast_t *attribute_name = mpc_ast_get_child(attributes->children[i], "attribute_name|string|>");
		mpc_ast_t *name = mpc_ast_get_child(attribute_name,"regex");
		definitions[i]->name = duplicate(name->contents);

		/*get object_type of attribute_definition */
		mpc_ast_t *obj_type = mpc_ast_get_child(attributes->children[i], "object_type|string");
		if (obj_type==NULL)							    	{definitions[i]->obj_type = ET_;}
		else if (strcmp(obj_type->contents,"BU_")==0) 		{definitions[i]->obj_type = BU_;}
		else if (strcmp(obj_type->contents,"BO_")==0) 	    {definitions[i]->obj_type = BO_;}
		else if (strcmp(obj_type->contents,"SG_")==0) 	    {definitions[i]->obj_type = SG_;}
		else     	                                    	{definitions[i]->obj_type = EV_;}                                       

		/*get attribute value type of attribute_definition */
		mpc_ast_t *attribute_type = mpc_ast_get_child(attributes->children[i], "attribute_value_type|>");

		mpc_ast_t *att_type;
		if (attribute_type==NULL)
		{
			att_type = mpc_ast_get_child(attributes->children[i],"attribute_value_type|string");
		}
		else
		{
			att_type = mpc_ast_get_child(attribute_type,"string");
		}
		

		if (strcmp(att_type->contents,"INT")==0)
		{
			definitions[i]->att_type = INT_;
			int j = mpc_ast_get_index_lb(attribute_type, "integer|regex", 0);
			mpc_ast_t *value = mpc_ast_get_child_lb(attribute_type,"integer|regex",j);
			sscanf(value->contents,  "%d", &definitions[i]->value.INT_.min);

			value = mpc_ast_get_child_lb(attribute_type,"integer|regex",++j);
			
			sscanf(value->contents,  "%d", &definitions[i]->value.INT_.max);

		}
		else if (strcmp(att_type->contents,"HEX")==0)
		{
			definitions[i]->att_type = HEX_;
			int j = mpc_ast_get_index_lb(attribute_type, "integer|regex", 0);
			mpc_ast_t *value = mpc_ast_get_child_lb(attribute_type,"integer|regex",j);
			sscanf(value->contents,  "%d", &definitions[i]->value.HEX_.min);

			value = mpc_ast_get_child_lb(attribute_type,"integer|regex",++j);
			sscanf(value->contents,  "%d", &definitions[i]->value.HEX_.max);
		}
		else if (strcmp(att_type->contents,"FLOAT")==0)
		{
			definitions[i]->att_type = FLOAT_;
			int j = mpc_ast_get_index_lb(attribute_type, "float|regex", 0);
			mpc_ast_t *value = mpc_ast_get_child_lb(attribute_type,"float|regex",j);
			sscanf(value->contents,  "%f", &definitions[i]->value.FLOAT_.min);

			value = mpc_ast_get_child_lb(attribute_type,"float|regex",++j);
			sscanf(value->contents,  "%f", &definitions[i]->value.FLOAT_.max);
		}
		else if (strcmp(att_type->contents,"STRING")==0)
		{
			definitions[i]->att_type = STRING_;
		}
		else if (strcmp(att_type->contents,"ENUM")==0)
		{
			definitions[i]->att_type = ENUM_;
			char **enum_list = allocate(sizeof(*enum_list)*attribute_type->children_num+1);
			int count = 0;
			for(int j = 0 ; j>=0;)
			{
				j = mpc_ast_get_index_lb(attribute_type, "string|>", j);
				if(j >= 0)
				{
					mpc_ast_t *value = mpc_ast_get_child(attribute_type->children[j],"regex");
					enum_list[count++]= duplicate(value->contents);
					j++;
				}
			}

			definitions[i]->value.ENUM_.ENUM_list = allocate(sizeof(char *));

			definitions[i]->value.ENUM_.count = count;
			definitions[i]->value.ENUM_.ENUM_list = enum_list;
		}
	}
	attribute_definitions *r = allocate(sizeof(r));
	r->attribute_definition_count = attributes->children_num;
	r->attributes = definitions;

	/*set attribute definition ini value*/
	mpc_ast_t *attribute_defaults = mpc_ast_get_child(ast, "attribute_defaults|>");

	if(attribute_defaults>0)
	{
		for(int i = 0; i< attribute_defaults->children_num;i++)
		{
			mpc_ast_t *attribute_name = mpc_ast_get_child(attribute_defaults->children[i], "attribute_name|string|>");
			mpc_ast_t *name = mpc_ast_get_child(attribute_name,"regex");

			for(int j = 0; j<r->attribute_definition_count;j++)
			{
				if(strcmp(name->contents,r->attributes[j]->name)==0)
				{
					switch(r->attributes[j]->att_type)
					{
						case INT_:
							name = mpc_ast_get_child(attribute_defaults->children[i],"attribute_value|float|regex");
							sscanf(name->contents,  "%d", &r->attributes[j]->inivalue.signed_integer);
							break;

						case HEX_:
							name = mpc_ast_get_child(attribute_defaults->children[i],"attribute_value|float|regex");
							sscanf(name->contents,  "%x", &r->attributes[j]->inivalue.signed_integer);
							break;

						case FLOAT_:
							name = mpc_ast_get_child(attribute_defaults->children[i],"attribute_value|float|regex");
							sscanf(name->contents,  "%f", &r->attributes[j]->inivalue.FLOAT);
							break;

						case STRING_:
							attribute_name = mpc_ast_get_child(attribute_defaults->children[i],"attribute_value|string|>");
							if(attribute_name)
							{
								name = mpc_ast_get_child(attribute_name,"regex");
								r->attributes[j]->inivalue.char_string = duplicate(name->contents);
							}
							break;	

						case ENUM_:
							attribute_name = mpc_ast_get_child(attribute_defaults->children[i],"attribute_value|string|>");
							if(attribute_name)
							{
								name = mpc_ast_get_child(attribute_name,"regex");
								sscanf(name->contents,  "%u", &r->attributes[j]->inivalue.unsigned_integer);
							}
							break;
					}
					break;
				}
			}
		}
	}

	assign_attribute_to_object(dbc,r);
	assign_init_attribute(ast,dbc);

}


dbc_t *ast2dbc(mpc_ast_t *ast)
{
	dbc_t *d = dbc_new();

	// find and store the vals into the dbc: they will be assigned to
	// signals later
	mpc_ast_t *vals_ast = mpc_ast_get_child_lb(ast, "vals|>", 0);
	if (vals_ast) {
		d->val_count = vals_ast->children_num;
		d->vals = allocate(sizeof(*d->vals) * (d->val_count+1));
		if (d->val_count) {
			int j = 0;
			for(int i = 0; i >= 0;) {
				i = mpc_ast_get_index_lb(vals_ast, "val|>", i);
				if (i >= 0) {
					mpc_ast_t *val_ast = mpc_ast_get_child_lb(vals_ast, "val|>", i);
					d->vals[j++] = ast2val(ast, val_ast);
					i++;
				}
			}
		}
	}

	int index     = mpc_ast_get_index_lb(ast, "messages|>", 0);
	mpc_ast_t *msgs_ast = mpc_ast_get_child_lb(ast, "messages|>", 0);
	if (index < 0) {
		warning("no messages found");
		return NULL;
	}

	int n = msgs_ast->children_num;
	if (n <= 0) {
		warning("messages has no children");
		return NULL;
	}

	can_msg_t **r = allocate(sizeof(*r) * (n+1));
	int j = 0;
	for(int i = 0; i >= 0;) {
		i = mpc_ast_get_index_lb(msgs_ast, "message|>", i);
		if (i >= 0) {
			mpc_ast_t *msg_ast = mpc_ast_get_child_lb(msgs_ast, "message|>", i);
			r[j++] = ast2msg(ast, msg_ast, d);
			i++;
		}
	}
	d->message_count = j;
	d->messages = r;

	int i = mpc_ast_get_index_lb(ast, "sigval|>", 0);
	if (i >= 0)
		d->use_float = true;

	// find and store the vals into the dbc: they will be assigned to
	// signals later
	mpc_ast_t *comments_ast = mpc_ast_get_child_lb(ast, "comments|>", 0);
	if (comments_ast && comments_ast->children_num) {
		for(int i = 0; i >= 0;) {
			i = mpc_ast_get_index_lb(comments_ast, "comment|>", i);
			if (i >= 0) {
				mpc_ast_t *comment_ast = mpc_ast_get_child_lb(comments_ast, "comment|>", i);
				if (comments_ast && comments_ast->children_num > 3) {
					bool to_message = strcmp(comment_ast->children[2]->contents, "BO_") == 0;
					bool to_signal = strcmp(comment_ast->children[2]->contents, "SG_") == 0;
					if (to_signal || to_message) {
						mpc_ast_t *id   = mpc_ast_get_child(comment_ast, "id|integer|regex");
						unsigned message_id;
						int r = sscanf(id->contents, "%u", &message_id);
						assert(r == 1);
						mpc_ast_t *comment = mpc_ast_get_child(comment_ast, "comment_string|string|>");
						if (to_signal) {
							mpc_ast_t *signal_name = mpc_ast_get_child(comment_ast, "name|ident|regex");
							assign_comment_to_signal(d, comment->children[1]->contents, message_id, signal_name->contents);
						} else  {
							assign_comment_to_message(d, comment->children[1]->contents, message_id);
						}
					}
				}
				i++;
			}
		}
	}

	/* assign attribute to node env message signals*/
	ast2attributedefinitions(ast,d);

	return d;
}


