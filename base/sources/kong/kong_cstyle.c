#include "kong.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

void cstyle_write_opcode(char *code, size_t *offset, opcode *o, type_string_func type_string, int *indentation) {
	switch (o->type) {
	case OPCODE_VAR:
		indent(code, offset, *indentation);
		if (get_type(o->op_var.var.type.type)->array_size > 0) {
			*offset += sprintf(&code[*offset], "%s _%" PRIu64 "[%i];\n", type_string(get_type(o->op_var.var.type.type)->base), o->op_var.var.index,
			                   get_type(o->op_var.var.type.type)->array_size);
		}
		else {
			*offset += sprintf(&code[*offset], "%s _%" PRIu64 ";\n", type_string(o->op_var.var.type.type), o->op_var.var.index);
		}
		break;
	case OPCODE_NEGATE:
		indent(code, offset, *indentation);
		*offset += sprintf(&code[*offset], "%s _%" PRIu64 " = -_%" PRIu64 ";\n", type_string(o->op_negate.to.type.type), o->op_negate.to.index,
		                   o->op_negate.from.index);
		break;
	case OPCODE_NOT:
		indent(code, offset, *indentation);
		*offset += sprintf(&code[*offset], "%s _%" PRIu64 " = !_%" PRIu64 ";\n", type_string(o->op_not.to.type.type), o->op_not.to.index, o->op_not.from.index);
		break;
	case OPCODE_STORE_VARIABLE:
		indent(code, offset, *indentation);
		*offset += sprintf(&code[*offset], "_%" PRIu64 " = _%" PRIu64 ";\n", o->op_store_var.to.index, o->op_store_var.from.index);
		break;
	case OPCODE_SUB_AND_STORE_VARIABLE:
		indent(code, offset, *indentation);
		*offset += sprintf(&code[*offset], "_%" PRIu64 " -= _%" PRIu64 ";\n", o->op_store_var.to.index, o->op_store_var.from.index);
		break;
	case OPCODE_ADD_AND_STORE_VARIABLE:
		indent(code, offset, *indentation);
		*offset += sprintf(&code[*offset], "_%" PRIu64 " += _%" PRIu64 ";\n", o->op_store_var.to.index, o->op_store_var.from.index);
		break;
	case OPCODE_DIVIDE_AND_STORE_VARIABLE:
		indent(code, offset, *indentation);
		*offset += sprintf(&code[*offset], "_%" PRIu64 " /= _%" PRIu64 ";\n", o->op_store_var.to.index, o->op_store_var.from.index);
		break;
	case OPCODE_MULTIPLY_AND_STORE_VARIABLE:
		indent(code, offset, *indentation);
		*offset += sprintf(&code[*offset], "_%" PRIu64 " *= _%" PRIu64 ";\n", o->op_store_var.to.index, o->op_store_var.from.index);
		break;
	case OPCODE_LOAD_ACCESS_LIST: {
		indent(code, offset, *indentation);
		*offset += sprintf(&code[*offset], "%s _%" PRIu64 " = _%" PRIu64, type_string(o->op_load_access_list.to.type.type), o->op_load_access_list.to.index,
		                   o->op_load_access_list.from.index);

		type *s = get_type(o->op_load_access_list.from.type.type);

		for (size_t i = 0; i < o->op_load_access_list.access_list_size; ++i) {
			switch (o->op_load_access_list.access_list[i].kind) {
			case ACCESS_ELEMENT:
				*offset += sprintf(&code[*offset], "[_%" PRIu64 "]", o->op_load_access_list.access_list[i].access_element.index.index);
				break;
			case ACCESS_MEMBER:
				*offset += sprintf(&code[*offset], ".%s", get_name(o->op_load_access_list.access_list[i].access_member.name));
				break;
			case ACCESS_SWIZZLE: {
				char swizzle[4];

				for (uint32_t swizzle_index = 0; swizzle_index < o->op_load_access_list.access_list[i].access_swizzle.swizzle.size; ++swizzle_index) {
					swizzle[swizzle_index] = "xyzw"[o->op_load_access_list.access_list[i].access_swizzle.swizzle.indices[swizzle_index]];
				}
				swizzle[o->op_load_access_list.access_list[i].access_swizzle.swizzle.size] = 0;

				*offset += sprintf(&code[*offset], ".%s", swizzle);
				break;
			}
			}

			s = get_type(o->op_load_access_list.access_list[i].type);
		}

		*offset += sprintf(&code[*offset], ";\n");

		break;
	}
	case OPCODE_STORE_ACCESS_LIST:
	case OPCODE_SUB_AND_STORE_ACCESS_LIST:
	case OPCODE_ADD_AND_STORE_ACCESS_LIST:
	case OPCODE_DIVIDE_AND_STORE_ACCESS_LIST:
	case OPCODE_MULTIPLY_AND_STORE_ACCESS_LIST: {
		indent(code, offset, *indentation);
		*offset += sprintf(&code[*offset], "_%" PRIu64, o->op_store_access_list.to.index);

		type *s = get_type(o->op_store_access_list.to.type.type);

		for (size_t i = 0; i < o->op_store_access_list.access_list_size; ++i) {
			switch (o->op_store_access_list.access_list[i].kind) {
			case ACCESS_ELEMENT:
				*offset += sprintf(&code[*offset], "[_%" PRIu64 "]", o->op_store_access_list.access_list[i].access_element.index.index);
				break;
			case ACCESS_MEMBER:
				*offset += sprintf(&code[*offset], ".%s", get_name(o->op_store_access_list.access_list[i].access_member.name));
				break;
			case ACCESS_SWIZZLE: {
				char swizzle[4];

				for (uint32_t swizzle_index = 0; swizzle_index < o->op_store_access_list.access_list[i].access_swizzle.swizzle.size; ++swizzle_index) {
					swizzle[swizzle_index] = "xyzw"[o->op_store_access_list.access_list[i].access_swizzle.swizzle.indices[swizzle_index]];
				}
				swizzle[o->op_store_access_list.access_list[i].access_swizzle.swizzle.size] = 0;

				*offset += sprintf(&code[*offset], ".%s", swizzle);

				break;
			}
			}

			s = get_type(o->op_store_access_list.access_list[i].type);
		}

		switch (o->type) {
		case OPCODE_STORE_ACCESS_LIST:
			*offset += sprintf(&code[*offset], " = _%" PRIu64 ";\n", o->op_store_access_list.from.index);
			break;
		case OPCODE_SUB_AND_STORE_ACCESS_LIST:
			*offset += sprintf(&code[*offset], " -= _%" PRIu64 ";\n", o->op_store_access_list.from.index);
			break;
		case OPCODE_ADD_AND_STORE_ACCESS_LIST:
			*offset += sprintf(&code[*offset], " += _%" PRIu64 ";\n", o->op_store_access_list.from.index);
			break;
		case OPCODE_DIVIDE_AND_STORE_ACCESS_LIST:
			*offset += sprintf(&code[*offset], " /= _%" PRIu64 ";\n", o->op_store_access_list.from.index);
			break;
		case OPCODE_MULTIPLY_AND_STORE_ACCESS_LIST:
			*offset += sprintf(&code[*offset], " *= _%" PRIu64 ";\n", o->op_store_access_list.from.index);
			break;
		default:
			kong_assert(false);
			break;
		}
		break;
	}
	case OPCODE_LOAD_FLOAT_CONSTANT:
		indent(code, offset, *indentation);
		*offset += sprintf(&code[*offset], "%s _%" PRIu64 " = %f;\n", type_string(o->op_load_float_constant.to.type.type), o->op_load_float_constant.to.index,
		                   o->op_load_float_constant.number);
		break;
	case OPCODE_LOAD_INT_CONSTANT:
		indent(code, offset, *indentation);
		*offset += sprintf(&code[*offset], "%s _%" PRIu64 " = %i;\n", type_string(o->op_load_int_constant.to.type.type), o->op_load_int_constant.to.index,
		                   o->op_load_int_constant.number);
		break;
	case OPCODE_LOAD_BOOL_CONSTANT:
		indent(code, offset, *indentation);
		*offset += sprintf(&code[*offset], "%s _%" PRIu64 " = %s;\n", type_string(o->op_load_bool_constant.to.type.type), o->op_load_bool_constant.to.index,
		                   o->op_load_bool_constant.boolean ? "true" : "false");
		break;
	case OPCODE_ADD: {
		indent(code, offset, *indentation);
		*offset += sprintf(&code[*offset], "%s _%" PRIu64 " = _%" PRIu64 " + _%" PRIu64 ";\n", type_string(o->op_binary.result.type.type),
		                   o->op_binary.result.index, o->op_binary.left.index, o->op_binary.right.index);
		break;
	}
	case OPCODE_SUB: {
		indent(code, offset, *indentation);
		*offset += sprintf(&code[*offset], "%s _%" PRIu64 " = _%" PRIu64 " - _%" PRIu64 ";\n", type_string(o->op_binary.result.type.type),
		                   o->op_binary.result.index, o->op_binary.left.index, o->op_binary.right.index);
		break;
	}
	case OPCODE_MULTIPLY: {
		indent(code, offset, *indentation);
		*offset += sprintf(&code[*offset], "%s _%" PRIu64 " = _%" PRIu64 " * _%" PRIu64 ";\n", type_string(o->op_binary.result.type.type),
		                   o->op_binary.result.index, o->op_binary.left.index, o->op_binary.right.index);
		break;
	}
	case OPCODE_DIVIDE: {
		indent(code, offset, *indentation);
		*offset += sprintf(&code[*offset], "%s _%" PRIu64 " = _%" PRIu64 " / _%" PRIu64 ";\n", type_string(o->op_binary.result.type.type),
		                   o->op_binary.result.index, o->op_binary.left.index, o->op_binary.right.index);
		break;
	}
	case OPCODE_MOD: {
		indent(code, offset, *indentation);
		*offset += sprintf(&code[*offset], "%s _%" PRIu64 " = _%" PRIu64 " %% _%" PRIu64 ";\n", type_string(o->op_binary.result.type.type),
		                   o->op_binary.result.index, o->op_binary.left.index, o->op_binary.right.index);
		break;
	}
	case OPCODE_EQUALS: {
		indent(code, offset, *indentation);
		*offset += sprintf(&code[*offset], "%s _%" PRIu64 " = _%" PRIu64 " == _%" PRIu64 ";\n", type_string(o->op_binary.result.type.type),
		                   o->op_binary.result.index, o->op_binary.left.index, o->op_binary.right.index);
		break;
	}
	case OPCODE_NOT_EQUALS: {
		indent(code, offset, *indentation);
		*offset += sprintf(&code[*offset], "%s _%" PRIu64 " = _%" PRIu64 " != _%" PRIu64 ";\n", type_string(o->op_binary.result.type.type),
		                   o->op_binary.result.index, o->op_binary.left.index, o->op_binary.right.index);
		break;
	}
	case OPCODE_GREATER: {
		indent(code, offset, *indentation);
		*offset += sprintf(&code[*offset], "%s _%" PRIu64 " = _%" PRIu64 " > _%" PRIu64 ";\n", type_string(o->op_binary.result.type.type),
		                   o->op_binary.result.index, o->op_binary.left.index, o->op_binary.right.index);
		break;
	}
	case OPCODE_GREATER_EQUAL: {
		indent(code, offset, *indentation);
		*offset += sprintf(&code[*offset], "%s _%" PRIu64 " = _%" PRIu64 " >= _%" PRIu64 ";\n", type_string(o->op_binary.result.type.type),
		                   o->op_binary.result.index, o->op_binary.left.index, o->op_binary.right.index);
		break;
	}
	case OPCODE_LESS: {
		indent(code, offset, *indentation);
		*offset += sprintf(&code[*offset], "%s _%" PRIu64 " = _%" PRIu64 " < _%" PRIu64 ";\n", type_string(o->op_binary.result.type.type),
		                   o->op_binary.result.index, o->op_binary.left.index, o->op_binary.right.index);
		break;
	}
	case OPCODE_LESS_EQUAL: {
		indent(code, offset, *indentation);
		*offset += sprintf(&code[*offset], "%s _%" PRIu64 " = _%" PRIu64 " <= _%" PRIu64 ";\n", type_string(o->op_binary.result.type.type),
		                   o->op_binary.result.index, o->op_binary.left.index, o->op_binary.right.index);
		break;
	}
	case OPCODE_AND: {
		indent(code, offset, *indentation);
		*offset += sprintf(&code[*offset], "%s _%" PRIu64 " = _%" PRIu64 " && _%" PRIu64 ";\n", type_string(o->op_binary.result.type.type),
		                   o->op_binary.result.index, o->op_binary.left.index, o->op_binary.right.index);
		break;
	}
	case OPCODE_OR: {
		indent(code, offset, *indentation);
		*offset += sprintf(&code[*offset], "%s _%" PRIu64 " = _%" PRIu64 " || _%" PRIu64 ";\n", type_string(o->op_binary.result.type.type),
		                   o->op_binary.result.index, o->op_binary.left.index, o->op_binary.right.index);
		break;
	}
	case OPCODE_BITWISE_XOR: {
		indent(code, offset, *indentation);
		*offset += sprintf(&code[*offset], "%s _%" PRIu64 " = _%" PRIu64 " ^ _%" PRIu64 ";\n", type_string(o->op_binary.result.type.type),
		                   o->op_binary.result.index, o->op_binary.left.index, o->op_binary.right.index);
		break;
	}
	case OPCODE_BITWISE_AND: {
		indent(code, offset, *indentation);
		*offset += sprintf(&code[*offset], "%s _%" PRIu64 " = _%" PRIu64 " & _%" PRIu64 ";\n", type_string(o->op_binary.result.type.type),
		                   o->op_binary.result.index, o->op_binary.left.index, o->op_binary.right.index);
		break;
	}
	case OPCODE_BITWISE_OR: {
		indent(code, offset, *indentation);
		*offset += sprintf(&code[*offset], "%s _%" PRIu64 " = _%" PRIu64 " | _%" PRIu64 ";\n", type_string(o->op_binary.result.type.type),
		                   o->op_binary.result.index, o->op_binary.left.index, o->op_binary.right.index);
		break;
	}
	case OPCODE_LEFT_SHIFT: {
		indent(code, offset, *indentation);
		*offset += sprintf(&code[*offset], "%s _%" PRIu64 " = _%" PRIu64 " << _%" PRIu64 ";\n", type_string(o->op_binary.result.type.type),
		                   o->op_binary.result.index, o->op_binary.left.index, o->op_binary.right.index);
		break;
	}
	case OPCODE_RIGHT_SHIFT: {
		indent(code, offset, *indentation);
		*offset += sprintf(&code[*offset], "%s _%" PRIu64 " = _%" PRIu64 " >> _%" PRIu64 ";\n", type_string(o->op_binary.result.type.type),
		                   o->op_binary.result.index, o->op_binary.left.index, o->op_binary.right.index);
		break;
	}
	case OPCODE_IF: {
		indent(code, offset, *indentation);
		*offset += sprintf(&code[*offset], "if (_%" PRIu64 ")\n", o->op_if.condition.index);
		break;
	}
	case OPCODE_WHILE_START: {
		indent(code, offset, *indentation);
		*offset += sprintf(&code[*offset], "while (true)\n");
		*offset += sprintf(&code[*offset], "{\n");
		break;
	}
	case OPCODE_WHILE_CONDITION: {
		indent(code, offset, *indentation);
		*offset += sprintf(&code[*offset], "if (!_%" PRIu64 ") { break; }\n", o->op_while.condition.index); // wgsl requires {} for if statements
		break;
	}
	case OPCODE_WHILE_END: {
		indent(code, offset, *indentation);
		*offset += sprintf(&code[*offset], "}\n");
		break;
	}
	case OPCODE_BLOCK_START: {
		indent(code, offset, *indentation);
		*offset += sprintf(&code[*offset], "{\n");
		*indentation += 1;
		break;
	}
	case OPCODE_BLOCK_END: {
		*indentation -= 1;
		indent(code, offset, *indentation);
		*offset += sprintf(&code[*offset], "}\n");
		break;
	}
	default: {
		debug_context context = {0};
		error(context, "Unknown opcode");
		break;
	}
	}
}
