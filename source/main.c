/**
 * @file main.c
 * @brief Application entry point and runtime bootstrap for the PS Vita iOS loader.
 */
#include <psp2/apputil.h>
#include <psp2/common_dialog.h>
#include <psp2/message_dialog.h>
#include <vitaGL.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>
#include <psp2/system_param.h>
#include <kubridge.h>
#include <stdio.h>
#include <string.h>

#include "darwin.h"
#include "gles.h"
#include "hle.h"
#include "input.h"
#include "log.h"
#include "macho.h"
#include "objc.h"
#include "patch.h"

int _newlib_heap_size_user = 256 * 1024 * 1024;
// Guest main thread requires substantial stack headroom (4 MB)
unsigned int sceUserMainThreadStackSize = 4 * 1024 * 1024;

// Console crash report: on a data/prefetch abort or undefined instruction the log gets PC/LR/regs as
// Mach-O offsets (symbols.txt) plus probable return addresses from the stack, then the default
// handler still writes the .psp2dmp. Without it a crash leaves the log cut mid-line (log 001).
static KuKernelExceptionHandler old_exc[3];
static uintptr_t main_stack_lo, main_stack_hi;
extern char _init, _fini; // loader .text bounds (linker)
static void on_exception(KuKernelExceptionContext *c) {
    static const char *kind[] = {"data abort", "prefetch abort", "undefined instruction"};
    uint32_t t = c->exceptionType < 3 ? c->exceptionType : 0;
#define MO(x) ((x) >= MACHO_SLIDE && (x) < MACHO_SLIDE + 0x100000u ? (x) - MACHO_SLIDE : 0)
    l_error("[CRASH] %s pc=%08X (macho 0x%X) lr=%08X (macho 0x%X) FAR=%08X FSR=%08X", kind[t], c->pc, MO(c->pc),
            c->lr, MO(c->lr), c->FAR, c->FSR);
    l_error("[CRASH] r0=%08X r1=%08X r2=%08X r3=%08X r4=%08X r5=%08X r6=%08X r7=%08X", c->r0, c->r1, c->r2, c->r3,
            c->r4, c->r5, c->r6, c->r7);
    l_error("[CRASH] r8=%08X r9=%08X r10=%08X r11=%08X r12=%08X sp=%08X", c->r8, c->r9, c->r10, c->r11, c->r12,
            c->sp);
    // Scan only up to the stack top: log 011 crashed 0x600 bytes below it and the 2 KB scan faulted.
    uintptr_t sp = c->sp, top = sp >= main_stack_lo && sp < main_stack_hi ? main_stack_hi : (sp | 0xFFF) + 1;
    char bt[256], be[256];
    int n = 0, k = 0, ne = 0, ke = 0;
    for (const uint32_t *st = (const uint32_t *)sp; (uintptr_t)st < top && (uintptr_t)st < sp + 2048; st++) {
        uint32_t w = *st;
        if (!(w & 1)) // Thumb return addresses only
            continue;
        if (MO(w) && k < 16)
            n += snprintf(bt + n, sizeof(bt) - n, " 0x%X", w - MACHO_SLIDE), k++;
        else if (w >= (uintptr_t)&_init && w < (uintptr_t)&_fini && ke < 16) // loader/vitaGL frames
            ne += snprintf(be + ne, sizeof(be) - ne, " %08X", w), ke++;
    }
    bt[n] = be[ne] = 0;
    l_error("[CRASH] stack (macho):%s", bt);
    l_error("[CRASH] stack (elf, addr2line):%s", be);
#undef MO
    if (old_exc[t])
        old_exc[t](c);
    sceKernelExitProcess(0); // returning would re-run the faulting instruction forever (log 006)
}

// Rotating log file manager (realracinghd_001.log .. realracinghd_999.log)
static int next_log_number(void) {
    sceIoMkdir(DATA_PATH, 0777);
    sceIoMkdir(LOGS_PATH, 0777);
    int last = 0, n;
    uint64_t newest = 0;
    SceUID d = sceIoDopen(LOGS_PATH);
    SceIoDirent e;
    while (d >= 0 && sceIoDread(d, &e) > 0) {
        const SceDateTime *t = &e.d_stat.st_mtime;
        uint64_t key = (uint64_t)t->year << 48 | (uint64_t)t->month << 40 | (uint64_t)t->day << 32 |
                       (uint64_t)t->hour << 24 | t->minute << 16 | t->second << 8 | t->microsecond / 10000;
        if (sscanf(e.d_name, "realracinghd_%3d.log", &n) == 1 && n >= 1 && n <= 999 && key >= newest) {
            newest = key;
            last = n;
        }
    }
    if (d >= 0)
        sceIoDclose(d);
    return (last % 999) + 1;
}

// Boot checker: a missing file is shown on screen (system message dialog) with its exact path,
// not just logged; the app exits when the dialog is closed.
// First-boot res/ -> rb/XX/ move (darwin_res_buckets): a bar on the 960x544 screen, no suspend.
static void boot_progress(int done, int total) {
    sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
    GLint fb;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb); // gles_init left the game's 768x1024 FBO bound
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, 960, 544);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glScissor(80, 260, 800, 24);
    glClearColor(0.15f, 0.15f, 0.15f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glScissor(80, 260, 800 * done / (total ? total : 1), 24);
    glClearColor(1, 0.85f, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 1);
    vglSwapBuffers(GL_FALSE);
    glBindFramebuffer(GL_FRAMEBUFFER, fb);
}

static void boot_check(const char *path, const char *what) {
    SceIoStat st;
    if (sceIoGetstat(path, &st) >= 0)
        return;
    static char msg[512];
    snprintf(msg, sizeof(msg), "Missing %s:\n%s\n\nCopy the decrypted Real Racing HD 1.04 .app contents to\nux0:data/realracinghd/game_app/ (see README).",
             what, path);
    l_error("[BOOT] %s", msg);
    SceMsgDialogUserMessageParam um = {0};
    um.buttonType = SCE_MSG_DIALOG_BUTTON_TYPE_OK;
    um.msg = (const SceChar8 *)msg;
    SceMsgDialogParam p;
    sceMsgDialogParamInit(&p);
    p.mode = SCE_MSG_DIALOG_MODE_USER_MSG;
    p.userMsgParam = &um;
    if (sceMsgDialogInit(&p) >= 0) {
        while (sceMsgDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED) {
            glClear(GL_COLOR_BUFFER_BIT);
            vglSwapBuffers(GL_TRUE);
        }
        sceMsgDialogTerm();
    }
    fatal_error("missing %s: %s", what, path);
}

int main(int argc, char *argv[]) {
    // 444 MHz is the stock-firmware maximum (higher values are rejected and the CPU stays at 333).
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);

    // Prepare save data, cache and logs directories
    sceIoMkdir(DATA_PATH, 0777);
    sceIoMkdir(DOCS_PATH, 0777);
    sceIoMkdir(DATA_PATH "Library", 0777);
    sceIoMkdir(DATA_PATH "Library/Caches", 0777);
    char log_path[128];
    snprintf(log_path, sizeof(log_path), LOGS_PATH "realracinghd_%03d.log", next_log_number());
    log_open(log_path);
    SceKernelThreadInfo ti = {.size = sizeof(ti)};
    if (sceKernelGetThreadInfo(sceKernelGetThreadId(), &ti) >= 0) {
        main_stack_lo = (uintptr_t)ti.stack;
        main_stack_hi = main_stack_lo + ti.stackSize;
    }
    l_info("=== iOS Mach-O Loader for PS Vita Initializing ===");
    for (int t = 0; t < 3; t++)
        if (kuKernelRegisterExceptionHandler(t, on_exception, &old_exc[t], NULL) < 0)
            l_warn("kuKernelRegisterExceptionHandler(%d) failed: crashes will not be logged", t);

    // Verify ShaRKBR33D runtime availability
    if (sceIoGetstat("ur0:/data/libshacccg.suprx", &(SceIoStat){0}) < 0 &&
        sceIoGetstat("ur0:/data/external/libshacccg.suprx", &(SceIoStat){0}) < 0) {
        fatal_error("Missing libshacccg.suprx (install it via ShaRKBR33D on console).");
    }

    // Initialize AppUtil and system common dialogs
    SceAppUtilInitParam init;
    SceAppUtilBootParam boot;
    memset(&init, 0, sizeof(init));
    memset(&boot, 0, sizeof(boot));
    sceAppUtilInit(&init, &boot);

    SceCommonDialogConfigParam dlg;
    sceCommonDialogConfigParamInit(&dlg);
    sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, (int *)&dlg.language);
    sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_ENTER_BUTTON, (int *)&dlg.enterButtonAssign);
    sceCommonDialogSetConfigParam(&dlg);

    // Initialize subsystems
    gles_init();
    darwin_init();
    input_init();
    objc_init(); // host classes must exist before imports are bound
    uikit_init();

    boot_check(EXE_PATH, "game executable (decrypted IPA, game_app/" EXE_NAME ")");
    // rebase.bin ships in the VPK; a staged ux0 copy (tools/stage_data.sh) wins for development.
    SceIoStat rst;
    const char *rebase = sceIoGetstat(DATA_PATH "rebase.bin", &rst) >= 0 ? DATA_PATH "rebase.bin" : "app0:rebase.bin";
    boot_check(rebase, "rebase table (tools/macho_prep.py)");

    // Load guest Mach-O executable from the staged bundle (BUNDLE_PATH)
    darwin_res_buckets(boot_progress);
    l_info("Loading primary Mach-O executable (%s)...", rebase);
    int res = macho_load(EXE_PATH, rebase);
    if (res < 0) {
        // Fallback error if game binary cannot be accessed
        fatal_error("Failed to load Mach-O executable at %s", EXE_PATH);
    }

    // Initialize game-specific hooks and memory patches
    patch_init();

    // Execute C/C++ static constructors (__mod_init_func)
    macho_run_inits();

    // Locate entry point
    uintptr_t entry = macho_main();
    if (!entry)
        fatal_error("guest entry point main() not found (no _main, LC_MAIN or crt start)");
    // main() -> UIApplicationMain() (uikit.c), which runs the run loop and never returns.
    l_info("Jumping to guest entry point at 0x%08X (Mach-O offset 0x%08X)", (unsigned)entry,
           (unsigned)(entry - MACHO_SLIDE));
    static char arg0[] = EXE_PATH;
    static char *argv_guest[] = {arg0, NULL};
    call_guest((void *)entry, 1, (uint32_t)argv_guest, 0, 0);
    fatal_error("guest main() returned");
}
