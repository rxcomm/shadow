/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */
#include "main/host/process.h"

#include <bits/stdint-intn.h>
#include <bits/stdint-uintn.h>
#include <bits/types/clockid_t.h>
#include <bits/types/struct_timespec.h>
#include <bits/types/struct_timeval.h>
#include <bits/types/struct_tm.h>
#include <bits/types/time_t.h>
#include <fcntl.h>
#include <features.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <ifaddrs.h>
#include <limits.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <sys/file.h>
#include <sys/un.h>
#include <syscall.h>
#include <time.h>
#include <unistd.h>


#include "glib/gprintf.h"
#include "main/core/support/definitions.h"
#include "main/core/support/object_counter.h"
#include "main/core/work/task.h"
#include "main/core/worker.h"
#include "main/host/cpu.h"
#include "main/host/descriptor/channel.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/shd-thread.h"
#include "main/host/descriptor/socket.h"
#include "main/host/descriptor/tcp.h"
#include "main/host/descriptor/timer.h"
#include "main/host/host.h"
#include "main/host/process.h"
#include "main/host/tracker.h"
#include "main/routing/address.h"
#include "main/routing/dns.h"
#include "main/utility/random.h"
#include "main/utility/utility.h"
#include "support/logger/logger.h"

struct _Process {
    /* the handler of system calls made by the process */
    SysCallHandler* sys;

    /* unique id of the program that this process should run */
    guint processID;
    GString* processName;

    /* the shadow plugin executable */
    struct {
        /* the name and path to the executable that we will exec */
        GString* exeName;
        GString* exePath;

        /* the name and path to any plugin-specific preload library that we should
         * LD_PRELOAD before exec */
        GString* preloadName;
        GString* preloadPath;

        /* TRUE from when we've called into plug-in code until the call completes.
         * Note that the plug-in may get back into shadow code during execution, by
         * calling a function that we intercept. */
        gboolean isExecuting;
    } plugin;

    /* timer that tracks the amount of CPU time we spend on plugin execution and processing */
    GTimer* cpuDelayTimer;
    gdouble totalRunTime;

    /* process boot and shutdown variables */
    SimulationTime startTime;
    SimulationTime stopTime;

    /* vector of argument strings passed to exec */
    gchar** argv;
    /* vector on environment variables passed to exec */
    gchar** envv;

    gint returnCode;
    gboolean didLogReturnCode;

    /* the main execution unit for the plugin */
    Thread* mainThread;
    gint threadIDCounter;

    // TODO add spawned threads

    gint referenceCount;
    MAGIC_DECLARE;
};

static const gchar* _process_getName(Process* proc) {
    MAGIC_ASSERT(proc);
    utility_assert(proc->processName->str);
    return proc->processName->str;
}

static void _process_handleTimerResult(Process* proc, gdouble elapsedTimeSec) {
    SimulationTime delay = (SimulationTime) (elapsedTimeSec * SIMTIME_ONE_SECOND);
    Host* currentHost = worker_getActiveHost();
    cpu_addDelay(host_getCPU(currentHost), delay);
    tracker_addProcessingTime(host_getTracker(currentHost), delay);
    proc->totalRunTime += elapsedTimeSec;
}

static void _process_logReturnCode(Process* proc, gint code) {
    if(!proc->didLogReturnCode) {
        GString* mainResultString = g_string_new(NULL);
        g_string_printf(mainResultString, "main %s code '%i' for process '%s'",
                ((code==0) ? "success" : "error"),
                code, _process_getName(proc));

        if(code == 0) {
            message("%s", mainResultString->str);
        } else {
            warning("%s", mainResultString->str);
            worker_incrementPluginError();
        }

        g_string_free(mainResultString, TRUE);

        proc->didLogReturnCode = TRUE;
    }
}

static void _process_check(Process* proc) {
    MAGIC_ASSERT(proc);

    if(!proc->mainThread) {
        return;
    }

    if(thread_isRunning(proc->mainThread)) {
        info("process '%s' is running, but threads are blocked waiting for events", _process_getName(proc));
    } else {
        /* collect return code */
        int returnCode = thread_getReturnCode(proc->mainThread);

        message("process '%s' has completed or is otherwise no longer running", _process_getName(proc));
        _process_logReturnCode(proc, returnCode);

        thread_unref(proc->mainThread);
        proc->mainThread = NULL;

        message("total runtime for process '%s' was %f seconds", _process_getName(proc), proc->totalRunTime);
    }
}

static void _process_start(Process* proc) {
    MAGIC_ASSERT(proc);

    /* dont do anything if we are already running */
    if(process_isRunning(proc)) {
        return;
    }

    utility_assert(proc->mainThread == NULL);
    proc->mainThread = thread_new(proc->threadIDCounter++, proc->sys);

    message("starting process '%s'", _process_getName(proc));

    /* now we will execute in the pth/plugin context, so we need to load the state */
    worker_setActiveProcess(proc);

    /* time how long we execute the program */
    g_timer_start(proc->cpuDelayTimer);

    proc->plugin.isExecuting = TRUE;
    /* exec the process and call main to start it */
    thread_run(proc->mainThread, proc->argv, proc->envv);
    gdouble elapsed = g_timer_elapsed(proc->cpuDelayTimer, NULL);
    _process_handleTimerResult(proc, elapsed);

    worker_setActiveProcess(NULL);

    message("process '%s' started in %f seconds", _process_getName(proc), elapsed);

    _process_check(proc);
}

void process_continue(Process* proc) {
    MAGIC_ASSERT(proc);

    /* if we are not running, no need to notify anyone */
    if(!process_isRunning(proc)) {
        return;
    }

    info("switching to thread controller to continue executing process '%s'", _process_getName(proc));

    worker_setActiveProcess(proc);

    /* time how long we execute the program */
    g_timer_start(proc->cpuDelayTimer);

    proc->plugin.isExecuting = TRUE;
    thread_resume(proc->mainThread);
    proc->plugin.isExecuting = FALSE;

    gdouble elapsed = g_timer_elapsed(proc->cpuDelayTimer, NULL);
    _process_handleTimerResult(proc, elapsed);

    worker_setActiveProcess(NULL);

    info("process '%s' ran for %f seconds", _process_getName(proc), elapsed);

    _process_check(proc);
}

void process_stop(Process* proc) {
    MAGIC_ASSERT(proc);

    /* we only have state if we are running */
    if(!process_isRunning(proc)) {
        return;
    }

    message("terminating process '%s'", _process_getName(proc));

    worker_setActiveProcess(proc);

    /* time how long we execute the program */
    g_timer_start(proc->cpuDelayTimer);

    proc->plugin.isExecuting = TRUE;
    thread_terminate(proc->mainThread);
    proc->plugin.isExecuting = FALSE;

    gdouble elapsed = g_timer_elapsed(proc->cpuDelayTimer, NULL);
    _process_handleTimerResult(proc, elapsed);

    worker_setActiveProcess(NULL);

    message("process '%s' stopped in %f seconds", _process_getName(proc), elapsed);

    _process_check(proc);
}

static void _process_runStartTask(Process* proc, gpointer nothing) {
    _process_start(proc);
}

static void _process_runStopTask(Process* proc, gpointer nothing) {
    process_stop(proc);
}

void process_schedule(Process* proc, gpointer nothing) {
    MAGIC_ASSERT(proc);

    SimulationTime now = worker_getCurrentTime();

    if(proc->stopTime == 0 || proc->startTime < proc->stopTime) {
        SimulationTime startDelay = proc->startTime <= now ? 1 : proc->startTime - now;
        process_ref(proc);
        Task* startProcessTask = task_new((TaskCallbackFunc)_process_runStartTask,
                proc, NULL, (TaskObjectFreeFunc)process_unref, NULL);
        worker_scheduleTask(startProcessTask, startDelay);
        task_unref(startProcessTask);
    }

    if(proc->stopTime > 0 && proc->stopTime > proc->startTime) {
        SimulationTime stopDelay = proc->stopTime <= now ? 1 : proc->stopTime - now;
        process_ref(proc);
        Task* stopProcessTask = task_new((TaskCallbackFunc)_process_runStopTask,
                proc, NULL, (TaskObjectFreeFunc)process_unref, NULL);
        worker_scheduleTask(stopProcessTask, stopDelay);
        task_unref(stopProcessTask);
    }
}

gboolean process_isRunning(Process* proc) {
    MAGIC_ASSERT(proc);
    return (proc->mainThread != NULL && thread_isRunning(proc->mainThread)) ? TRUE : FALSE;
}

gboolean process_wantsNotify(Process* proc, gint epollfd) {
    MAGIC_ASSERT(proc);
    // FIXME TODO XXX
    // how do we hook up notifations for epollfds?
    return FALSE;
    // old code:
//    if(process_isRunning(proc) && epollfd == proc->epollfd) {
//        return TRUE;
//    } else {
//        return FALSE;
//    }
}

static gchar** _process_getArgv(Process* proc, gchar* arguments) {
    MAGIC_ASSERT(proc);

    /* we need at least the executable path in order to run the plugin */
    GString* command = g_string_new(proc->plugin.exePath->str);

    /* if the user specified additional arguments, append those */
    if(arguments && (g_ascii_strncasecmp(arguments, "\0", (gsize) 1) != 0)) {
        g_string_append_printf(command, " %s", arguments);
    }

    /* now split the command string to an argv */
    gchar** argv = g_strsplit(command->str, " ", 0);

    /* we don't need the command string anymore */
    g_string_free(command, TRUE);

    return argv;
}

static gchar** _process_getEnvv(Process* proc, gchar* environment) {
    MAGIC_ASSERT(proc);

    /* start with an empty environment */
    gchar** envv = g_environ_setenv(NULL, "SHADOW_SPAWNED", "TRUE", TRUE);

    /* set up the LD_PRELOAD environment */
    // TODO no hard code!
    envv = g_environ_setenv(envv, "LD_PRELOAD", "/home/rjansen/.shadow/lib/libshadow-shim.so", TRUE);

    // TODO add in user-specified env vals

    return envv;
}

void process_setSysCallHandler(Process* proc, SysCallHandler* sys) {
    MAGIC_ASSERT(proc);
    utility_assert(sys);
    proc->sys = sys;
    syscallhandler_ref(proc->sys);
}

Process* process_new(guint processID,
        SimulationTime startTime, SimulationTime stopTime, const gchar* hostName,
        const gchar* pluginName, const gchar* pluginPath, const gchar* pluginSymbol,
        const gchar* preloadName, const gchar* preloadPath, gchar* arguments) {
    Process* proc = g_new0(Process, 1);
    MAGIC_INIT(proc);

    proc->processID = processID;

    /* plugin name and path are required so we know what to execute */
    utility_assert(pluginName);
    utility_assert(pluginPath);
    proc->plugin.exeName = g_string_new(pluginName);
    proc->plugin.exePath = g_string_new(pluginPath);

    /* a user-specified preload library (in addition to shadow's) is optional */
    if(preloadName && preloadPath) {
        proc->plugin.preloadName = g_string_new(preloadName);
        proc->plugin.preloadPath = g_string_new(preloadPath);
    }

    proc->processName = g_string_new(NULL);
    g_string_printf(proc->processName, "%s.%s.%u",
            hostName,
            proc->plugin.exeName ? proc->plugin.exeName->str : "NULL",
            proc->processID);

    proc->cpuDelayTimer = g_timer_new();

    proc->startTime = startTime;
    proc->stopTime = stopTime;

    /* get args and env */
    proc->argv = _process_getArgv(proc, arguments);
    gchar* environment = NULL; // TODO allow user to specify env
    proc->envv = _process_getEnvv(proc, environment);

    proc->referenceCount = 1;

    worker_countObject(OBJECT_TYPE_PROCESS, COUNTER_TYPE_NEW);

    return proc;
}

static void _process_free(Process* proc) {
    MAGIC_ASSERT(proc);

    /* stop and free plugin memory if we are still running */
    if(proc->mainThread) {
        if(thread_isRunning(proc->mainThread)) {
            thread_terminate(proc->mainThread);
        }
        thread_unref(proc->mainThread);
        proc->mainThread = NULL;
    }

    if(proc->plugin.exePath) {
        g_string_free(proc->plugin.exePath, TRUE);
    }
    if(proc->plugin.exeName) {
        g_string_free(proc->plugin.exeName, TRUE);
    }
    if(proc->plugin.preloadPath) {
        g_string_free(proc->plugin.preloadPath, TRUE);
    }
    if(proc->plugin.preloadName) {
        g_string_free(proc->plugin.preloadName, TRUE);
    }
    if(proc->processName) {
        g_string_free(proc->processName, TRUE);
    }

    if(proc->argv) {
        g_strfreev(proc->argv);
    }
    if(proc->envv) {
        g_strfreev(proc->envv);
    }

    g_timer_destroy(proc->cpuDelayTimer);

    if(proc->sys) {
        syscallhandler_unref(proc->sys);
    }

    worker_countObject(OBJECT_TYPE_PROCESS, COUNTER_TYPE_FREE);

    MAGIC_CLEAR(proc);
    g_free(proc);
}

void process_ref(Process* proc) {
    MAGIC_ASSERT(proc);
    (proc->referenceCount)++;
}

void process_unref(Process* proc) {
    MAGIC_ASSERT(proc);
    (proc->referenceCount)--;
    utility_assert(proc->referenceCount >= 0);
    if(proc->referenceCount == 0) {
        _process_free(proc);
    }
}
