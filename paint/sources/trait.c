
#include "global.h"

typedef void (*trait_init_t)(char *object);
typedef void (*trait_run_t)();
typedef void (*trait_stop_t)();

typedef struct trait {
	char        *name;
	trait_init_t init;
	trait_run_t  run;
	trait_stop_t stop;
} trait_t;

static trait_t traits[] = {
    {"third_person_controller", trait_third_person_controller_init, trait_third_person_controller_run, trait_third_person_controller_stop},
    {"point_and_click_controller", trait_point_and_click_controller_init, trait_point_and_click_controller_run, trait_point_and_click_controller_stop},
};

#define TRAIT_COUNT (i32)(sizeof(traits) / sizeof(traits[0]))

static bool trait_attached[TRAIT_COUNT];

void script_add_trait(char *object, char *trait) {
	for (i32 i = 0; i < TRAIT_COUNT; ++i) {
		if (!string_equals(traits[i].name, trait)) {
			continue;
		}
		if (trait_attached[i]) {
			return;
		}
		trait_attached[i] = true;
		traits[i].init(object);
		return;
	}
	// console_error(string("Unknown trait: %s", trait));
}

void trait_update() {
	for (i32 i = 0; i < TRAIT_COUNT; ++i) {
		if (trait_attached[i]) {
			traits[i].run();
		}
	}
}

void trait_stop() {
	for (i32 i = 0; i < TRAIT_COUNT; ++i) {
		if (trait_attached[i] && traits[i].stop != NULL) {
			traits[i].stop();
		}
	}
}
