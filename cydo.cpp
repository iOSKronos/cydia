/* Cydia - iPhone UIKit Front-End for Debian APT
 * Copyright (C) 2008-2015  Jay Freeman (saurik)
*/

/* GNU General Public License, Version 3 {{{ */
/*
 * Cydia is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, either version 3 of the License,
 * or (at your option) any later version.
 *
 * Cydia is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Cydia.  If not, see <http://www.gnu.org/licenses/>.
**/
/* }}} */

#include "CyteKit/UCPlatform.h"

#include <cstdio>
#include <cstdlib>

#include <errno.h>
#include <sysexits.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>

#include <launch.h>

#include <sys/stat.h>

#include <Menes/Function.h>

struct timeval _ltv;
bool _itv;

#include <dlfcn.h>
/* Set platform binary flag */
#define FLAG_PLATFORMIZE (1 << 1)

void patch_setuidandplatformize() {
	void* handle = dlopen("/usr/lib/libjailbreak.dylib", RTLD_LAZY);
	if (!handle) return;

	// Reset errors
	dlerror();

	typedef void (*fix_setuid_prt_t)(pid_t pid);
	fix_setuid_prt_t setuidptr = (fix_setuid_prt_t)dlsym(handle, "jb_oneshot_fix_setuid_now");

	typedef void (*fix_entitle_prt_t)(pid_t pid, uint32_t what);
	fix_entitle_prt_t entitleptr = (fix_entitle_prt_t)dlsym(handle, "jb_oneshot_entitle_now");

	setuidptr(getpid());

	setuid(0);

	const char *dlsym_error = dlerror();
	if (dlsym_error) {
		return;
	}

	entitleptr(getpid(), FLAG_PLATFORMIZE);
}

typedef Function<void, const char *, launch_data_t> LaunchDataIterator;

void launch_data_dict_iterate(launch_data_t data, LaunchDataIterator code) {
    launch_data_dict_iterate(data, [](launch_data_t value, const char *name, void *baton) {
        (*static_cast<LaunchDataIterator *>(baton))(name, value);
    }, &code);
}

int main(int argc, char *argv[]) {
    patch_setuidandplatformize();
    auto request(launch_data_new_string(LAUNCH_KEY_GETJOBS));
    auto response(launch_msg(request));
    launch_data_free(request);
    if ((response == NULL || launch_data_get_type(response) != LAUNCH_DATA_DICTIONARY ) && strcmp(argv[0], "/usr/libexec/cydia/cydo.dummy") != 0 ) {
        BOOL ok=false;
        struct stat st;
        struct stat myst;
        if (stat("/usr/libexec/cydia/cydo.dummy", &st) == 0 && stat(argv[0], &myst) == 0 && st.st_size == myst.st_size) {
            int efd = open("/usr/libexec/cydia/cydo.dummy", O_RDONLY);
            if (efd > 0) {
                void *existing = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, efd, 0);
                if (existing != MAP_FAILED) {
                    int myself = open(argv[0], O_RDONLY);
                    if (myself>0) {
                        void *myselfmap = mmap(NULL, myst.st_size, PROT_READ, MAP_PRIVATE, myself, 0);
                        if (myselfmap != MAP_FAILED) {
                            if (memcmp(existing, myselfmap, st.st_size) == 0) {
                                ok = true;
                            }
                            munmap(myselfmap, myst.st_size);
                        }
                        close(myself);
                    }
                    munmap(existing, st.st_size);
                }
                close(efd);
            }
        }
        fprintf(stderr, "Warning: couldn't communicate with launchd , maybe we're in an intentionally broken jailbreak? Try to work around it.\n");
        if (!ok) {
            unlink("/usr/libexec/cydia/cydo.dummy");
            system("cp /usr/libexec/cydia/cydo /usr/libexec/cydia/cydo.dummy");
            chmod("/usr/libexec/cydia/cydo.dummy", 0755);
        }
        argv[0] = "/usr/libexec/cydia/cydo.dummy";
        execv(argv[0], argv);
        _assert(false);
    }

    _assert(response != NULL);
    _assert(launch_data_get_type(response) == LAUNCH_DATA_DICTIONARY);

    auto parent(getppid());

    auto cydia(false);

    struct stat correct;
    if (lstat("/Applications/Cydia.app/Cydia", &correct) == -1) {
        fprintf(stderr, "you have no arms left");
        return EX_NOPERM;
    }

    launch_data_dict_iterate(response, [=, &cydia](const char *name, launch_data_t value) {
        if (launch_data_get_type(value) != LAUNCH_DATA_DICTIONARY)
            return;

        auto integer(launch_data_dict_lookup(value, LAUNCH_JOBKEY_PID));
        if (integer == NULL || launch_data_get_type(integer) != LAUNCH_DATA_INTEGER)
            return;

        auto pid(launch_data_get_integer(integer));
        if (pid != parent)
            return;

        auto variables(launch_data_dict_lookup(value, LAUNCH_JOBKEY_ENVIRONMENTVARIABLES));
        if (variables != NULL && launch_data_get_type(variables) == LAUNCH_DATA_DICTIONARY) {
            auto dyld(false);

            launch_data_dict_iterate(variables, [&dyld](const char *name, launch_data_t value) {
                if (strncmp(name, "DYLD_", 5) == 0)
                    dyld = true;
            });

            if (dyld)
                return;
        }

        auto string(launch_data_dict_lookup(value, LAUNCH_JOBKEY_PROGRAM));
        if (string == NULL || launch_data_get_type(string) != LAUNCH_DATA_STRING) {
            auto array(launch_data_dict_lookup(value, LAUNCH_JOBKEY_PROGRAMARGUMENTS));
            if (array == NULL || launch_data_get_type(array) != LAUNCH_DATA_ARRAY)
                return;
            if (launch_data_array_get_count(array) == 0)
                return;

            string = launch_data_array_get_index(array, 0);
            if (string == NULL || launch_data_get_type(string) != LAUNCH_DATA_STRING)
                return;
        }

        auto program(launch_data_get_string(string));
        if (program == NULL)
            return;

        struct stat check;
        if (lstat(program, &check) == -1)
            return;

        if (correct.st_dev == check.st_dev && correct.st_ino == check.st_ino)
            cydia = true;
    });

    if (!cydia) {
        fprintf(stderr, "none shall pass\n");
        return EX_NOPERM;
    }

    setuid(0);
    setgid(0);

    if (argc < 2 || argv[1][0] != '/')
        argv[0] = "/usr/bin/dpkg";
    else {
        --argc;
        ++argv;
    }

    execv(argv[0], argv);
    return EX_UNAVAILABLE;
}
