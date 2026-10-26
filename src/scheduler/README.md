# Scheduler and CADU lifecycle (M8)

Time-of-day capture scheduling and the CADU spool lifecycle.

| File | Responsibility |
|---|---|
| `schedule_model.h` / `.cpp` | `ScheduleEntry`, `Schedule`, `FieldError`; `parse_schedule`, `schedule_to_json`, `validate_schedule` |
| `scheduler.h` / `.cpp` | 1 Hz (T5) time-of-day transitions, `ScheduleState`, `apply()`, janitor hook |
| `cadu_lifecycle.h` / `.cpp` | per-capture xRIT spool state machine + retention janitor |

## Schedule model

The schedule is the `schedule` object of the config: `enabled`, `timezone`,
`default_mode` and a list of `entries`. Each entry has a `name`, `start`/`end`
(`HH:MM`, converted to minutes-of-day via `time::parse_hhmm`), a `mode`
(`lrit`/`hrit`), an integer `priority` and optional `frequency_hz`,
`symbol_rate`, `sample_rate_hz` and `constellation` overrides. `schedule_to_json`
emits `start`/`end` as `HH:MM` strings so the result still passes
`Config::validate`.

## Scheduler

`Scheduler` runs a 1 Hz loop on its own thread. Each tick selects the active
entry for the current time in the configured timezone (highest priority wins on
overlap; `UTC`, `local` and fixed `UTC±H[:MM]` offsets are supported) and calls
the `TransitionFn` when the desired mode differs from the running one. The
transition is only taken when the active entry changed (or the schedule is
disabled), so the configured `run.mode` is preserved at startup when no entry is
active. `tick(std::time_t)` is public so tests are deterministic and never sleep.

`App` owns the `Scheduler`, supplies the `TransitionFn` (stop the old pipeline,
retune the SDR, start the new one) and serialises the swap with
`pipeline_mutex_`. The live mode is published to the web layer through an atomic
`mode_code` and a thread-safe `pipeline_snapshot` accessor.

## CADU lifecycle

`CaduLifecycle` creates `<storage.root>/spool/xrit/<capture_id>/` per capture
(`Spooling → Decoding → Verifying → Deleted|Retained`). A successful capture
deletes the spool; a failed one is retained for
`storage.retention.failed_cadu_hours` and removed by the janitor, which also
prunes old GGAK spool day-directories (`storage.retention.ggak_days`).
`janitor_at(std::time_t)` is the deterministic test hook.

## REST

`GET /api/v1/schedule` returns the schedule plus `ScheduleState`;
`PUT /api/v1/schedule` validates and applies it (200, or 400 with
`{"error":"validation_failed","fields":[...]}`). `/api/v1/status` populates its
`schedule` object from `Scheduler::state()`.
