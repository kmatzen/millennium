#define _POSIX_C_SOURCE 200112L
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../plugins.h"
#include "../plugin_sdk.h"
#include "../config.h"

#define STORY_MAX_SCENES 128
#define STORY_MAX_TRANSITIONS 768
#define STORY_MAX_VARIABLES 32
#define STORY_MAX_CALLBACKS 128
#define STORY_TEXT 81

typedef struct {
    char name[65];
    char line1[STORY_TEXT];
    char line2[STORY_TEXT];
    char audio[65];
    char ending[65];
    int timeout_seconds;
    char call[32];
    int ring;
} story_scene_t;

typedef struct {
    char scene[65];
    char event[16];
    char target[65];
    char when_var[65];
    int when_value;
    int has_condition;
    char set_var[65];
    int set_value;
    char increment_var[65];
    int increment_value;
} story_transition_t;

typedef struct {
    char name[65];
    int value;
} story_variable_t;

typedef struct {
    char scene[65];
    int after_seconds;
    char target[65];
} story_callback_t;

typedef struct {
    char id[65];
    char version[32];
    char entry[65];
    story_scene_t scenes[STORY_MAX_SCENES];
    story_transition_t transitions[STORY_MAX_TRANSITIONS];
    story_variable_t variables[STORY_MAX_VARIABLES];
    story_callback_t callbacks[STORY_MAX_CALLBACKS];
    int scene_count;
    int transition_count;
    int variable_count;
    int callback_count;
    int current;
    time_t entered_at;
    int loaded;
    time_t session_started_at;
    char repeat_key;
    int volume_percent;
    time_t callback_due_at;
    char callback_target[65];
} story_runtime_t;

static story_runtime_t runtime;

static void copy_field(char *destination, size_t size, const char *source) {
    if (!destination || size == 0) return;
    if (!source) source = "";
    strncpy(destination, source, size - 1);
    destination[size - 1] = '\0';
}

static const char *optional_field(const char *source) {
    return source && strcmp(source, "-") == 0 ? "" : source;
}

static int split_tabs(char *line, char **fields, int maximum) {
    int count = 0;
    char *cursor = line;
    char *tab;
    while (count < maximum) {
        fields[count++] = cursor;
        tab = strchr(cursor, '\t');
        if (!tab) break;
        *tab = '\0';
        cursor = tab + 1;
    }
    if (count > 0) fields[count - 1][strcspn(fields[count - 1], "\r\n")] = '\0';
    return count;
}

static int find_scene(const char *name) {
    int i;
    for (i = 0; i < runtime.scene_count; i++) {
        if (strcmp(runtime.scenes[i].name, name) == 0) return i;
    }
    return -1;
}

static int find_variable(const char *name, int create) {
    int i;
    for (i = 0; i < runtime.variable_count; i++) {
        if (strcmp(runtime.variables[i].name, name) == 0) return i;
    }
    if (!create || runtime.variable_count >= STORY_MAX_VARIABLES) return -1;
    i = runtime.variable_count++;
    copy_field(runtime.variables[i].name, sizeof(runtime.variables[i].name), name);
    runtime.variables[i].value = 0;
    return i;
}

static int variable_value(const char *name) {
    int index;
    time_t now;
    struct tm *local;
    if (strcmp(name, "$hour") == 0 || strcmp(name, "$weekday") == 0) {
        now = sdk_now();
        local = localtime(&now);
        if (!local) return 0;
        return strcmp(name, "$hour") == 0 ? local->tm_hour : local->tm_wday;
    }
    index = find_variable(name, 0);
    return index >= 0 ? runtime.variables[index].value : 0;
}

static const char *story_path(void) {
    return config_get_string(config_get_instance(), "story.path",
                             "/var/lib/millennium/content/current/story.mst");
}

static const char *state_path(void) {
    return config_get_string(config_get_instance(), "story.state_path",
                             "/var/lib/millennium/story-state");
}

static int callback_delivery_allowed(void) {
    config_data_t *config = config_get_instance();
    int start;
    int end;
    int hour;
    time_t now;
    struct tm *local;
    if (!config_get_bool(config, "story.callbacks_enabled", 1)) return 0;
    start = config_get_int(config, "story.callback_quiet_start", 22);
    end = config_get_int(config, "story.callback_quiet_end", 8);
    if (start < 0 || start > 23 || end < 0 || end > 23) return 0;
    if (start == end) return 1;
    now = sdk_now();
    local = localtime(&now);
    if (!local) return 0;
    hour = local->tm_hour;
    if (start < end) return !(hour >= start && hour < end);
    return !(hour >= start || hour < end);
}

static story_callback_t *callback_for_scene(const char *name) {
    int i;
    for (i = 0; i < runtime.callback_count; i++) {
        if (strcmp(runtime.callbacks[i].scene, name) == 0) return &runtime.callbacks[i];
    }
    return NULL;
}

static void save_state(void) {
    char temporary[512];
    FILE *stream;
    int i;
    if (!runtime.loaded || runtime.current < 0) return;
    if (snprintf(temporary, sizeof(temporary), "%s.tmp", state_path()) >= (int)sizeof(temporary)) return;
    stream = fopen(temporary, "w");
    if (!stream) return;
    fprintf(stream, "MSTATE\t1\t%s\t%s\t%s\n", runtime.id, runtime.version,
            runtime.scenes[runtime.current].name);
    for (i = 0; i < runtime.variable_count; i++) {
        fprintf(stream, "VAR\t%s\t%d\n", runtime.variables[i].name,
                runtime.variables[i].value);
    }
    if (runtime.callback_due_at > 0 && runtime.callback_target[0]) {
        fprintf(stream, "CALLBACK\t%lld\t%s\n",
                (long long)runtime.callback_due_at, runtime.callback_target);
    }
    if (fclose(stream) != 0 || rename(temporary, state_path()) != 0) {
        remove(temporary);
    }
}

static void show_current(void) {
    story_scene_t *scene;
    story_callback_t *callback;
    if (!runtime.loaded || runtime.current < 0) return;
    scene = &runtime.scenes[runtime.current];
    sdk_display(scene->line1, scene->line2);
    if (scene->ring) sdk_ring();
    else if (scene->audio[0]) sdk_play_content_clip(scene->audio);
    if (scene->call[0]) {
        const char *target = scene->call;
        if (strcmp(target, "configured") == 0)
            target = config_get_string(config_get_instance(), "story.call_target", "");
        if (target[0]) sdk_call(target);
    }
    runtime.entered_at = sdk_now();
    callback = callback_for_scene(scene->name);
    if (callback && runtime.callback_due_at == 0) {
        runtime.callback_due_at = sdk_now() + callback->after_seconds;
        memcpy(runtime.callback_target, callback->target,
               sizeof(runtime.callback_target));
        runtime.callback_target[sizeof(runtime.callback_target) - 1] = '\0';
        sdk_metric_increment("story_callbacks_scheduled");
    }
    save_state();
    sdk_logf("Story", "Entered %s/%s scene %s", runtime.id, runtime.version,
             scene->name);
}

static int load_story(void) {
    FILE *stream;
    char line[512];
    char *fields[10];
    int field_count;
    int header = 0;
    memset(&runtime, 0, sizeof(runtime));
    runtime.current = -1;
    stream = fopen(story_path(), "r");
    if (!stream) {
        sdk_logf("Story", "Cannot load %s: %s", story_path(), strerror(errno));
        return -1;
    }
    while (fgets(line, sizeof(line), stream)) {
        field_count = split_tabs(line, fields, 10);
        if (field_count == 5 && strcmp(fields[0], "MSTORY") == 0 &&
                (strcmp(fields[1], "1") == 0 || strcmp(fields[1], "2") == 0)) {
            copy_field(runtime.id, sizeof(runtime.id), fields[2]);
            copy_field(runtime.version, sizeof(runtime.version), fields[3]);
            copy_field(runtime.entry, sizeof(runtime.entry), fields[4]);
            header = 1;
        } else if (field_count == 6 && strcmp(fields[0], "ACCESS") == 0) {
            runtime.repeat_key = fields[1][0];
            runtime.volume_percent = atoi(fields[3]);
        } else if (field_count == 3 && strcmp(fields[0], "VAR") == 0 &&
                   runtime.variable_count < STORY_MAX_VARIABLES) {
            int index = find_variable(fields[1], 1);
            runtime.variables[index].value = atoi(fields[2]);
        } else if ((field_count == 8 || field_count == 9) &&
                   strcmp(fields[0], "SCENE") == 0 &&
                   runtime.scene_count < STORY_MAX_SCENES) {
            story_scene_t *scene = &runtime.scenes[runtime.scene_count++];
            copy_field(scene->name, sizeof(scene->name), fields[1]);
            copy_field(scene->line1, sizeof(scene->line1), fields[2]);
            copy_field(scene->line2, sizeof(scene->line2), fields[3]);
            copy_field(scene->audio, sizeof(scene->audio), optional_field(fields[4]));
            copy_field(scene->ending, sizeof(scene->ending), optional_field(fields[5]));
            scene->timeout_seconds = atoi(fields[6]);
            copy_field(scene->call, sizeof(scene->call), optional_field(fields[7]));
            scene->ring = field_count == 9 ? atoi(fields[8]) != 0 : 0;
        } else if (field_count == 10 && strcmp(fields[0], "TRANS") == 0 &&
                   runtime.transition_count < STORY_MAX_TRANSITIONS) {
            story_transition_t *transition = &runtime.transitions[runtime.transition_count++];
            copy_field(transition->scene, sizeof(transition->scene), fields[1]);
            copy_field(transition->event, sizeof(transition->event), fields[2]);
            copy_field(transition->target, sizeof(transition->target), fields[3]);
            copy_field(transition->when_var, sizeof(transition->when_var), optional_field(fields[4]));
            transition->has_condition = fields[4][0] != '\0' && strcmp(fields[4], "-") != 0;
            transition->when_value = atoi(fields[5]);
            copy_field(transition->set_var, sizeof(transition->set_var), optional_field(fields[6]));
            transition->set_value = atoi(fields[7]);
            copy_field(transition->increment_var, sizeof(transition->increment_var), optional_field(fields[8]));
            transition->increment_value = atoi(fields[9]);
        } else if (field_count == 4 && strcmp(fields[0], "CALLBACK") == 0 &&
                   runtime.callback_count < STORY_MAX_CALLBACKS) {
            story_callback_t *callback = &runtime.callbacks[runtime.callback_count++];
            copy_field(callback->scene, sizeof(callback->scene), fields[1]);
            callback->after_seconds = atoi(fields[2]);
            copy_field(callback->target, sizeof(callback->target), fields[3]);
            if (callback->after_seconds <= 0) {
                fclose(stream);
                sdk_log("Story", "Invalid callback delay");
                return -1;
            }
        } else {
            fclose(stream);
            sdk_log("Story", "Invalid or oversized compiled story");
            return -1;
        }
    }
    fclose(stream);
    runtime.current = find_scene(runtime.entry);
    if (!header || runtime.current < 0 || runtime.scene_count == 0) {
        sdk_log("Story", "Story header or entry scene is invalid");
        return -1;
    }
    runtime.loaded = 1;
    return 0;
}

static void restore_state(void) {
    FILE *stream;
    char line[256];
    char *fields[6];
    int count;
    int restored;
    stream = fopen(state_path(), "r");
    if (!stream) return;
    if (!fgets(line, sizeof(line), stream)) {
        fclose(stream);
        return;
    }
    count = split_tabs(line, fields, 6);
    if (count != 5 || strcmp(fields[0], "MSTATE") != 0 ||
            strcmp(fields[1], "1") != 0 || strcmp(fields[2], runtime.id) != 0 ||
            strcmp(fields[3], runtime.version) != 0) {
        fclose(stream);
        return;
    }
    restored = find_scene(fields[4]);
    if (restored >= 0) runtime.current = restored;
    while (fgets(line, sizeof(line), stream)) {
        int index;
        count = split_tabs(line, fields, 6);
        if (count == 3 && strcmp(fields[0], "VAR") == 0) {
            index = find_variable(fields[1], 0);
            if (index >= 0) runtime.variables[index].value = atoi(fields[2]);
        } else if (count == 3 && strcmp(fields[0], "CALLBACK") == 0) {
            long long due = strtoll(fields[1], NULL, 10);
            if (due > 0 && find_scene(fields[2]) >= 0) {
                runtime.callback_due_at = (time_t)due;
                copy_field(runtime.callback_target, sizeof(runtime.callback_target), fields[2]);
            }
        }
    }
    fclose(stream);
}

static int transition_matches(const story_transition_t *transition) {
    if (!transition->has_condition) return 1;
    return variable_value(transition->when_var) == transition->when_value;
}

static void apply_transition(story_transition_t *transition) {
    int index;
    if (transition->set_var[0]) {
        index = find_variable(transition->set_var, 1);
        if (index >= 0) runtime.variables[index].value = transition->set_value;
    }
    if (transition->increment_var[0]) {
        index = find_variable(transition->increment_var, 1);
        if (index >= 0) runtime.variables[index].value += transition->increment_value;
    }
}

static int dispatch_event(const char *event) {
    int i;
    int fallback = -1;
    const char *scene_name;
    if (!runtime.loaded || runtime.current < 0) return 0;
    scene_name = runtime.scenes[runtime.current].name;
    for (i = 0; i < runtime.transition_count; i++) {
        if (strcmp(runtime.transitions[i].scene, scene_name) != 0) continue;
        if (strcmp(runtime.transitions[i].event, "default") == 0 &&
                transition_matches(&runtime.transitions[i])) fallback = i;
        if (strcmp(runtime.transitions[i].event, event) == 0 &&
                transition_matches(&runtime.transitions[i])) {
            apply_transition(&runtime.transitions[i]);
            runtime.current = find_scene(runtime.transitions[i].target);
            sdk_metric_increment("story_branch_selections");
            if (runtime.current >= 0 && runtime.scenes[runtime.current].ending[0]) {
                sdk_metric_increment("story_completions");
                sdk_metric_observe("story_session_duration_seconds",
                                   (double)sdk_elapsed(runtime.session_started_at));
            }
            show_current();
            return 1;
        }
    }
    if (fallback >= 0) {
        apply_transition(&runtime.transitions[fallback]);
        runtime.current = find_scene(runtime.transitions[fallback].target);
        sdk_metric_increment("story_recovery_transitions");
        show_current();
        return 1;
    }
    sdk_metric_increment("story_unexpected_inputs");
    return 0;
}

static int story_key(char key) {
    char event[8];
    snprintf(event, sizeof(event), "key:%c", key);
    sdk_beep(key);
    if (key == runtime.repeat_key) {
        sdk_metric_increment("story_prompt_repeats");
        show_current();
        return 1;
    }
    return dispatch_event(event);
}

static int story_coin(int value, const char *code) {
    (void)value;
    (void)code;
    sdk_coin_chime();
    return dispatch_event("coin");
}

static int story_card(const char *token) {
    (void)token;
    return dispatch_event("card");
}

static int story_hook(int up, int down) {
    if (up) return dispatch_event("hook_up") || dispatch_event("resume");
    if (down) {
        sdk_metric_increment("story_interruptions");
        return dispatch_event("hook_down");
    }
    return 0;
}

static int story_call_state(int state) {
    if (state == EVENT_CALL_STATE_ACTIVE) return dispatch_event("call_connected");
    if (state == EVENT_CALL_STATE_INVALID) return dispatch_event("call_ended");
    return 0;
}

static void story_activate(void) {
    if (load_story() != 0) {
        sdk_display("STORY UNAVAILABLE", "TRY AGAIN LATER");
        return;
    }
    restore_state();
    runtime.session_started_at = sdk_now();
    sdk_set_volume_percent(runtime.volume_percent > 0 ? runtime.volume_percent : 100);
    sdk_metric_increment("story_sessions");
    show_current();
}

static void story_tick(void) {
    story_scene_t *scene;
    if (!runtime.loaded || runtime.current < 0) return;
    if (runtime.callback_due_at > 0 && sdk_now() >= runtime.callback_due_at &&
            !sdk_receiver_is_up() && callback_delivery_allowed()) {
        int target = find_scene(runtime.callback_target);
        runtime.callback_due_at = 0;
        runtime.callback_target[0] = '\0';
        if (target >= 0) {
            runtime.current = target;
            sdk_metric_increment("story_callbacks_delivered");
            show_current();
            return;
        }
    }
    scene = &runtime.scenes[runtime.current];
    if (scene->timeout_seconds > 0 && sdk_elapsed(runtime.entered_at) >= scene->timeout_seconds) {
        dispatch_event("timeout");
    }
}

void register_story_mode_plugin(void) {
    plugins_register("Story Mode", "Signed, data-authored branching experiences",
                     story_coin, story_key, story_hook, story_call_state, story_card,
                     story_activate, story_tick);
}
