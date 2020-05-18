#include "CyteKit/UCPlatform.h"

#include <dirent.h>
#include <strings.h>

#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/types.h>

#include <Menes/ObjectHandle.h>

/* Set platform binary flag */
#include <dlfcn.h>
#define FLAG_PLATFORMIZE (1 << 1)

void platformize_me() {
	void* handle = dlopen("/usr/lib/libjailbreak.dylib", RTLD_LAZY);
	if (!handle) return;

	// Reset errors
	dlerror();
	typedef void (*fix_entitle_prt_t)(pid_t pid, uint32_t what);
	fix_entitle_prt_t ptr = (fix_entitle_prt_t)dlsym(handle, "jb_oneshot_entitle_now");

	const char *dlsym_error = dlerror();
	if (dlsym_error) return;

	ptr(getpid(), FLAG_PLATFORMIZE);
}

void Finish(const char *finish) {
    if (finish == NULL)
        return;

    const char *cydia(getenv("CYDIA"));
    if (cydia == NULL)
        return;

    int fd([[[[NSString stringWithUTF8String:cydia] componentsSeparatedByString:@" "] objectAtIndex:0] intValue]);

    FILE *fout(fdopen(fd, "w"));
    fprintf(fout, "finish:%s\n", finish);
    fclose(fout);
}

void UICache() {
    const char *cydia(getenv("CYDIA"));
    if (cydia == NULL)
        return;

    int fd([[[[NSString stringWithUTF8String:cydia] componentsSeparatedByString:@" "] objectAtIndex:0] intValue]);

    FILE *fout(fdopen(fd, "w"));
    fprintf(fout, "uicache:yes\n");
    fclose(fout);
}

int main(int argc, const char *argv[]) {
    if (argc < 2)
        return 0;
    if (strcmp(argv[1], "triggered") == 0)
        UICache();
    if (strcmp(argv[1], "configure") != 0)
        return 0;

    UICache();

    platformize_me();

    NSAutoreleasePool *pool([[NSAutoreleasePool alloc] init]);

    bool restart(false);

    #define OldCache_ "/var/root/Library/Caches/com.saurik.Cydia"
    if (access(OldCache_, F_OK) == 0)
        system("rm -rf " OldCache_);

    #define NewCache_ "/var/mobile/Library/Caches/com.saurik.Cydia"
    system("cd /; su mobile -c 'mkdir -p " NewCache_ "'");
    if (access(NewCache_ "/lists", F_OK) != 0 && errno == ENOENT)
        system("cp -at " NewCache_ " /var/lib/apt/lists");
    system("chown -R 501.501 " NewCache_);

    #define OldLibrary_ "/var/lib/cydia"

    #define NewLibrary_ "/var/mobile/Library/Cydia"
    system("cd /; su mobile -c 'mkdir -p " NewLibrary_ "'");

    #define Cytore_ "/metadata.cb0"

    #define CYDIA_LIST "/etc/apt/sources.list.d/cydia.list"
    unlink(CYDIA_LIST);
    [[NSString stringWithFormat:@
        "deb http://apt.thebigboss.org/repofiles/cydia/ stable main\n"
        "deb https://repo.chariz.com/ ./\n"
        "deb https://repo.dynastic.co/ ./\n"
    ] writeToFile:@ CYDIA_LIST atomically:YES];

    if (access(NewLibrary_ Cytore_, F_OK) != 0 && errno == ENOENT) {
        if (access(NewCache_ Cytore_, F_OK) == 0)
            system("mv -f " NewCache_ Cytore_ " " NewLibrary_);
        else if (access(OldLibrary_ Cytore_, F_OK) == 0)
            system("mv -f " OldLibrary_ Cytore_ " " NewLibrary_);
        chown(NewLibrary_ Cytore_, 501, 501);
    }

    if (restart)
        Finish("restart");

    [pool release];
    return 0;
}
