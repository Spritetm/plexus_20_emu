#include "emscripten_env.h"
#include "emscripten.h"

void emscripten_init() {
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
