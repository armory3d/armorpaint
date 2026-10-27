#pragma once

// fn takes and returns the types of the sig, globals come from minic_register_global()
void minic_tcc_register(const char *name, const char *sig, void *fn);
void minic_tcc_register_builtins(void); // Provided by the host, called before the first script
void minic_tcc_eval(char *text, char *name);
