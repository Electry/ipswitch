#include "config.h"
#include "patch.h"

int checkRequirement() {
    if(!isDirectory(SXOS_DIR)) {
    	userConfirm("Do you even SX OS bro?");
    	return -1;
    }
    return 0;
}
