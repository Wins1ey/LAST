use livesplit_auto_splitting::{
    time, SettingValue, SettingsStore, Timer, TimerState, UserSettingKind,
};
use std::{
    borrow::Cow,
    cell::RefCell,
    ffi::{c_void, CStr},
    fmt, fs,
};

thread_local! {
    static OUTPUT_VEC: RefCell<Vec<u8>>  = RefCell::new(Vec::new());
}

fn output_vec<F>(f: F) -> *const u8
where
    F: FnOnce(&mut Vec<u8>),
{
    OUTPUT_VEC.with(|output| {
        let mut output = output.borrow_mut();
        output.clear();
        f(&mut output);
        output.push(0);
        output.as_ptr()
    })
}

fn output_str(s: &str) -> *const u8 {
    output_vec(|o| {
        o.extend_from_slice(s.as_bytes());
    })
}

unsafe fn str(s: *const u8) -> Cow<'static, str> {
    if s.is_null() {
        "".into()
    } else {
        CStr::from_ptr(s.cast()).to_string_lossy()
    }
}

pub struct Runtime {
    runtime: livesplit_auto_splitting::Runtime<CTimer>,
    tick_rate: std::time::Duration,
}

#[no_mangle]
pub extern "C" fn SettingsStore_new() -> Box<SettingsStore> {
    {
        Box::new(SettingsStore::new())
    }
}

#[no_mangle]
pub extern "C" fn SettingsStore_drop(_: Box<SettingsStore>) {}

/// # Safety
/// TODO:
#[no_mangle]
pub unsafe extern "C" fn SettingsStore_set_bool(
    _this: &mut SettingsStore,
    _key_ptr: *const u8,
    _value: bool,
) {
    {
        _this.set(str(_key_ptr as _).into(), SettingValue::Bool(_value));
    }
}

/// # Safety
/// TODO:
#[no_mangle]
pub unsafe extern "C" fn Runtime_new(
    path_ptr: *const u8,
    settings_store: Box<SettingsStore>,
    context: *mut c_void,
    state: unsafe extern "C" fn(*mut c_void) -> i32,
    start: unsafe extern "C" fn(*mut c_void),
    split: unsafe extern "C" fn(*mut c_void),
    skip_split: unsafe extern "C" fn(*mut c_void),
    undo_split: unsafe extern "C" fn(*mut c_void),
    reset: unsafe extern "C" fn(*mut c_void),
    set_game_time: unsafe extern "C" fn(*mut c_void, i64),
    pause_game_time: unsafe extern "C" fn(*mut c_void),
    resume_game_time: unsafe extern "C" fn(*mut c_void),
    log: unsafe extern "C" fn(*mut c_void, *const u8),
) -> Option<Box<Runtime>> {
    {
        let path = str(path_ptr);
        let file = fs::read(&*path).ok()?;
        let runtime = livesplit_auto_splitting::Runtime::new(
            &file,
            CTimer {
                context,
                state,
                start,
                split,
                skip_split,
                undo_split,
                reset,
                set_game_time,
                pause_game_time,
                resume_game_time,
                log,
            },
            *settings_store,
        )
        .ok()?;

        Some(Box::new(Runtime {
            runtime,
            tick_rate: std::time::Duration::new(1, 0) / 120,
        }))
    }
}

#[no_mangle]
pub extern "C" fn Runtime_drop(_: Box<Runtime>) {}

#[no_mangle]
pub extern "C" fn Runtime_step(_this: &mut Runtime) -> bool {
    {
        if let Ok(tick_rate) = _this.runtime.update() {
            _this.tick_rate = tick_rate;
            true
        } else {
            false
        }
    }
}

#[no_mangle]
pub extern "C" fn Runtime_tick_rate(_this: &Runtime) -> u64 {
    const MICROS_PER_SEC: u64 = 1_000_000;
    const NANOS_PER_SEC: u64 = 1_000_000_000;
    const NANOS_PER_MICRO: u64 = NANOS_PER_SEC / MICROS_PER_SEC;

    let tick_rate = _this.tick_rate;

    let (secs, nanos) = (tick_rate.as_secs(), tick_rate.subsec_nanos());

    secs * MICROS_PER_SEC + nanos as u64 / NANOS_PER_MICRO
}

#[no_mangle]
pub extern "C" fn Runtime_user_settings_len(_this: &Runtime) -> usize {
    {
        _this.runtime.user_settings().len()
    }
}

#[no_mangle]
pub extern "C" fn Runtime_user_settings_get_key(_this: &Runtime, _index: usize) -> *const u8 {
    {
        output_str(&_this.runtime.user_settings()[_index].key)
    }
}

#[no_mangle]
pub extern "C" fn Runtime_user_settings_get_description(
    _this: &Runtime,
    _index: usize,
) -> *const u8 {
    {
        output_str(&_this.runtime.user_settings()[_index].description)
    }
}

#[no_mangle]
pub extern "C" fn Runtime_user_settings_get_type(_this: &Runtime, _index: usize) -> usize {
    {
        match _this.runtime.user_settings()[_index].kind {
            UserSettingKind::Bool { .. } => 1,
            _ => 0,
        }
    }
}

#[no_mangle]
pub extern "C" fn Runtime_user_settings_get_bool(_this: &Runtime, _index: usize) -> bool {
    {
        let setting = &_this.runtime.user_settings()[_index];
        let UserSettingKind::Bool { default_value } = setting.kind else { return false };
        match _this.runtime.settings_store().get(&setting.key) {
            Some(SettingValue::Bool(stored)) => *stored,
            _ => default_value,
        }
    }
}

pub struct CTimer {
    context: *mut c_void,
    state: unsafe extern "C" fn(*mut c_void) -> i32,
    start: unsafe extern "C" fn(*mut c_void),
    split: unsafe extern "C" fn(*mut c_void),
    skip_split: unsafe extern "C" fn(*mut c_void),
    undo_split: unsafe extern "C" fn(*mut c_void),
    reset: unsafe extern "C" fn(*mut c_void),
    set_game_time: unsafe extern "C" fn(*mut c_void, i64),
    pause_game_time: unsafe extern "C" fn(*mut c_void),
    resume_game_time: unsafe extern "C" fn(*mut c_void),
    log: unsafe extern "C" fn(*mut c_void, *const u8),
}

impl Timer for CTimer {
    fn state(&self) -> TimerState {
        match unsafe { (self.state)(self.context) } {
            1 => TimerState::Running,
            2 => TimerState::Paused,
            3 => TimerState::Ended,
            _ => TimerState::NotRunning,
        }
    }

    fn start(&mut self) {
        unsafe { (self.start)(self.context) }
    }

    fn split(&mut self) {
        unsafe { (self.split)(self.context) }
    }

    fn skip_split(&mut self) {
        unsafe { (self.skip_split)(self.context) }
    }

    fn undo_split(&mut self) {
        unsafe { (self.undo_split)(self.context) }
    }

    fn reset(&mut self) {
        unsafe { (self.reset)(self.context) }
    }

    fn set_game_time(&mut self, time: time::Duration) {
        const TICKS_PER_SEC: i64 = 10_000_000;
        const NANOS_PER_SEC: i64 = 1_000_000_000;
        const NANOS_PER_TICK: i64 = NANOS_PER_SEC / TICKS_PER_SEC;

        let (secs, nanos) = (time.whole_seconds(), time.subsec_nanoseconds());
        let ticks = secs * TICKS_PER_SEC + nanos as i64 / NANOS_PER_TICK;
        unsafe { (self.set_game_time)(self.context, ticks) }
    }

    fn pause_game_time(&mut self) {
        unsafe { (self.pause_game_time)(self.context) }
    }

    fn resume_game_time(&mut self) {
        unsafe { (self.resume_game_time)(self.context) }
    }

    fn set_variable(&mut self, _: &str, _: &str) {}

    fn log(&mut self, message: fmt::Arguments<'_>) {
        let mut message = message.to_string();
        message.push('\0');
        unsafe { (self.log)(self.context, message.as_ptr()) }
    }
}

/// Returns the byte length of the last nul-terminated string returned on the
/// current thread. The length excludes the nul-terminator.
#[no_mangle]
pub extern "C" fn get_buf_len() -> usize {
    {
        OUTPUT_VEC.with(|v| v.borrow().len() - 1)
    }
}