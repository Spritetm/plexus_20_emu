/*
SPDX-License-Identifier: MIT
Copyright (c) 2024 Sprite_tm <jeroen@spritesmods.com>
*/

#include "emscripten_env.h"
#include "emscripten.h"

void emscripten_init() {
	//We want an IDBFS filesystem on /persist where we can store RTC and COW stuff.
	EM_ASM(
		FS.mkdir("/persist");
		FS.mount(IDBFS, {}, "/persist");
		FS.syncfs(true, function (err) {
			assert(!err);
		});
	);
}

void emscripten_syncfs() {
	EM_ASM(
		FS.syncfs(false, function (err) {
			assert(!err);
		});
	);
}
