/*-------------------------------------------------------------------------
 *
 * pg_misc_functions.c
 *		  Module to provide various miscellaneous PostgreSQL functions
 *
 * Copyright (c) 2022, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *		  contrib/pg_misc_functions/pg_misc_functions.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/xlog.h"
#include "access/xlogrecovery.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "replication/walreceiver.h"
#include "storage/proc.h"
#include "storage/procarray.h"

PG_MODULE_MAGIC;

/*---- Structures ----*/

/*---- Macros ----*/

/*---- Global variables ----*/

/*---- Function declarations ----*/
static bool SignalBackend(int pid, int signum);

PG_FUNCTION_INFO_V1(pg_cause_panic);
PG_FUNCTION_INFO_V1(pg_cause_fatal);
PG_FUNCTION_INFO_V1(pg_signal_backend_with_pid);
PG_FUNCTION_INFO_V1(pg_current_wal_tli);
PG_FUNCTION_INFO_V1(pg_last_wal_replay_tli);
PG_FUNCTION_INFO_V1(pg_last_wal_receive_tli);

/*
 * SQL function for generating a PANIC to take down the entire running database
 * cluster.
 */
Datum
pg_cause_panic(PG_FUNCTION_ARGS)
{
	/* only superuser can execute this function */
	if (!superuser())
	{
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("must be a superuser to execute pg_cause_panic function"),
				 errdetail("This function needs to be strictly used only for testing or development purposes not on production servers.")));
	}

	ereport(PANIC,
			(errcode(ERRCODE_INTERNAL_ERROR),
			 errmsg("PANIC generated by pg_cause_panic function")));

	PG_RETURN_VOID();
}

/*
 * SQL function for generating a FATAL to abort the backend.
 */
Datum
pg_cause_fatal(PG_FUNCTION_ARGS)
{
	/* only superuser can execute this function */
	if (!superuser())
	{
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("must be a superuser to execute pg_cause_fatal function"),
				 errdetail("This function needs to be strictly used only for testing or development purposes not on production servers.")));
	}

	ereport(FATAL,
			(errcode(ERRCODE_INTERNAL_ERROR),
			 errmsg("FATAL error generated by pg_cause_fatal function")));

	PG_RETURN_VOID();
}

/*
 * Signals a PostgreSQL backend (including auxiliary processes and postmaster)
 * of given PID with given signal.
 */
static bool
SignalBackend(int pid, int signum)
{
	/*
	 * See if the process with given pid is postmaster process.
	 */
	if (pid == PostmasterPid)
	{
		/* do nothing for now, may be it's good idea to issue a warning? */
	}
	else
	{
		PGPROC	   *proc;

		/*
		 * See if the process with given pid is a backend process.
		 */
		proc = BackendPidGetProc(pid);

		/*
		 * See if the process with given pid is an auxiliary process.
		 */
		if (proc == NULL)
			proc = AuxiliaryPidGetProc(pid);

		/*
		 * BackendPidGetProc() and AuxiliaryPidGetProc() return NULL if the pid
		 * isn't valid; but by the time we reach kill(), a process for which we
		 * get a valid proc here might have terminated on its own.  There's no
		 * way to acquire a lock on an arbitrary process to prevent that.
		 */
		if (proc == NULL)
		{
			/*
			* This is just a warning so a loop-through-resultset will not abort
			* if one backend terminated on its own during the run.
			*/
			ereport(WARNING,
					(errmsg("PID %d is not a PostgreSQL server process", pid)));

			return false;
		}
	}

	/*
	 * XXX: should we be setting the reason for SIGUSR1, a multiplexed signal?
	 * Without any reason procsignal_sigusr1_handler() will just sets latch
	 * which should be fine.
	 */

	/*
	 * Can the process we just validated above end, followed by the pid being
	 * recycled for a new process, before reaching here?  Then we'd be trying
	 * to kill the wrong thing.  Seems near impossible when sequential pid
	 * assignment and wraparound is used.  Perhaps it could happen on a system
	 * where pid re-use is randomized.  That race condition possibility seems
	 * too unlikely to worry about.
	 */

	/* If we have setsid(), signal the backend's whole process group */
#ifdef HAVE_SETSID
	if (kill(-pid, signum))
#else
	if (kill(pid, signum))
#endif
	{
		/* Again, just a warning to allow loops */
		ereport(WARNING,
				(errmsg("could not send signal %d to process %d: %m", signum, pid)));

		return false;
	}

	return true;
}

/*
 * SQL function for signaling a PostgreSQL backend (including auxiliary
 * processes and postmaster) of given PID with given signal.
 */
Datum
pg_signal_backend_with_pid(PG_FUNCTION_ARGS)
{
	int			pid = PG_GETARG_INT32(0);
	int			signum = PG_GETARG_INT32(1);

	/* only superuser can execute this function */
	if (!superuser())
	{
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("must be a superuser to execute pg_signal_backend function")));
	}

	PG_RETURN_BOOL(SignalBackend(pid, signum));
}

/*
 * Returns current insert timeline ID of the server.
 */
Datum
pg_current_wal_tli(PG_FUNCTION_ARGS)
{
	TimeLineID tli;

	(void) GetFlushRecPtr(&tli);

	if (tli == 0)
		PG_RETURN_NULL();

	PG_RETURN_UINT32(tli);
}

/*
 * Returns timeline ID of last replayed WAL record.
 */
Datum
pg_last_wal_replay_tli(PG_FUNCTION_ARGS)
{
	TimeLineID tli;

	(void) GetXLogReplayRecPtr(&tli);

	if (tli == 0)
		PG_RETURN_NULL();

	PG_RETURN_UINT32(tli);
}

/*
 * Returns timeline ID of last WAL record received by WAL receiver.
 */
Datum
pg_last_wal_receive_tli(PG_FUNCTION_ARGS)
{
	TimeLineID tli;

	(void) GetWalRcvFlushRecPtr(NULL, &tli);

	if (tli == 0)
		PG_RETURN_NULL();

	PG_RETURN_UINT32(tli);
}
