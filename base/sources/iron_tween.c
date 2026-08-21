#include "iron_tween.h"

#include "iron_alloc.h"
#include "iron_array.h"
#include <math.h>
#include <stdbool.h>

static any_array_t *_tween_anims      = NULL;
static bool         _tween_registered = false;

f32  sys_delta(void);
void sys_notify_on_update(void (*f)(void *data), void *data);

static void _tween_register(void) {
	_tween_registered = true;
	_tween_anims      = any_array_create(0);
	sys_notify_on_update(tween_update, NULL);
}

tween_anim_t *tween_to(tween_anim_t *anim) {
	if (!_tween_registered) {
		_tween_register();
	}
	if (anim->target != NULL) {
		anim->_from = *anim->target;
	}
	any_array_push(_tween_anims, anim);
	return anim;
}

tween_anim_t *tween_timer(f32 delay, void (*done)(void *data), void *data) {
	tween_anim_t *a = calloc(1, sizeof(tween_anim_t));
	a->target       = NULL;
	a->to           = 0.0f;
	a->duration     = 0.0f;
	a->delay        = delay;
	a->ease         = EASE_LINEAR;
	a->done         = done;
	a->tick         = NULL;
	a->done_data    = data;
	a->_from        = 0.0f;
	a->_time        = 0.0f;
	return tween_to(a);
}

void tween_stop(tween_anim_t *anim) {
	array_remove(_tween_anims, anim);
}

void tween_reset(void) {
	_tween_anims = any_array_create(0);
}

static f32 _tween_ease_linear(f32 k) {
	return k;
}

static f32 _tween_ease_expo_in(f32 k) {
	return k == 0.0f ? 0.0f : powf(2.0f, 10.0f * (k - 1.0f));
}

static f32 _tween_ease_expo_out(f32 k) {
	return k == 1.0f ? 1.0f : (1.0f - powf(2.0f, -10.0f * k));
}

static f32 _tween_ease(ease_t ease, f32 k) {
	if (ease == EASE_LINEAR) {
		return _tween_ease_linear(k);
	}
	if (ease == EASE_EXPO_IN) {
		return _tween_ease_expo_in(k);
	}
	if (ease == EASE_EXPO_OUT) {
		return _tween_ease_expo_out(k);
	}
	return 0.0f;
}

void tween_update(void *_) {
	if (_tween_anims == NULL) {
		return;
	}
	f32 d = sys_delta();
	i32 i = (i32)_tween_anims->length;
	while (i-- > 0 && _tween_anims->length > 0) {
		tween_anim_t *a = _tween_anims->buffer[i];

		if (a->delay > 0.0f) {
			a->delay -= d;
			continue;
		}

		a->_time += d;
		if (a->_time >= a->duration) {
			array_splice(_tween_anims, (uint32_t)i, 1);
			i--;
			if (a->done != NULL) {
				a->done(a->done_data);
			}
			continue;
		}

		f32 k = a->_time / a->duration;
		if (k > 1.0f) {
			k = 1.0f;
		}

		if (a->target != NULL) {
			*a->target = a->_from + (a->to - a->_from) * _tween_ease(a->ease, k);
		}

		if (a->tick != NULL) {
			a->tick();
		}
	}
}
