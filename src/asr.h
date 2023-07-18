#ifndef __ASR_H__
#define __ASR_H__

typedef struct SettingsStore SettingsStore;
typedef struct Runtime Runtime;

SettingsStore* SettingsStore_new(void);

Runtime* Runtime_new(
    const char* path,
    SettingsStore* settings_store,
    void* context,
    int32_t (*state)(void*),
    void (*start)(void*),
    void (*split)(void*),
    void (*skip_split)(void*),
    void (*undo_split)(void*),
    void (*reset)(void*),
    void (*set_game_time)(void*, int64_t),
    void (*pause_game_time)(void*),
    void (*resume_game_time)(void*),
    void (*log)(void*, const char*)
);

void Runtime_drop(Runtime* runtime);
bool Runtime_step(Runtime* runtime);
uint64_t Runtime_tick_rate(Runtime const* runtime);

#endif /* __ASR_H__ */