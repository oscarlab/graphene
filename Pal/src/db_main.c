/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Copyright (C) 2014 Stony Brook University */

/*
 * This file contains the main function of the PAL loader, which loads and processes environment,
 * arguments and manifest.
 */

#include <stdbool.h>

#include "api.h"
#include "elf/elf.h"
#include "pal.h"
#include "pal_defs.h"
#include "pal_error.h"
#include "pal_internal.h"
#include "pal_rtld.h"
#include "sysdeps/generic/ldsodefs.h"
#include "toml.h"

PAL_CONTROL g_pal_control = {
    /* Enable log to catch early initialization errors; it will be overwritten in pal_main(). */
    .log_level = PAL_LOG_DEFAULT_LEVEL,
};

const PAL_CONTROL* DkGetPalControl(void) {
    return &g_pal_control;
}

struct pal_internal_state g_pal_state;

static void load_libraries(void) {
    int ret = 0;
    char* preload_str = NULL;

    /* FIXME: rewrite to use array-of-strings TOML syntax */
    /* string with preload libs: can be multiple URIs separated by commas */
    ret = toml_string_in(g_pal_state.manifest_root, "loader.preload", &preload_str);
    if (ret < 0)
        INIT_FAIL_MANIFEST(PAL_ERROR_INVAL, "Cannot parse 'loader.preload'");

    if (!preload_str)
        return;

    size_t len = strlen(preload_str);
    if (len == 0)
        return;

    char* c = preload_str;
    char* library_name = c;
    for (;; c++) {
        if (*c == ',' || !*c) {
            if (c > library_name) {
                *c = 0;
                if ((ret = load_elf_object(library_name, OBJECT_PRELOAD)) < 0)
                    INIT_FAIL(-ret, "Unable to load preload library");
            }

            if (c == preload_str + len)
                break;

            library_name = c + 1;
        }
    }
}

/* This function leaks memory on failure (and this is non-trivial to fix), but the assumption is
 * that its failure finishes the execution of the whole process right away. */
static int insert_envs_from_manifest(const char*** envpp) {
    int ret;
    assert(envpp);

    toml_table_t* toml_loader = toml_table_in(g_pal_state.manifest_root, "loader");
    if (!toml_loader)
        return 0;

    toml_table_t* toml_envs = toml_table_in(toml_loader, "env");
    if (!toml_envs)
        return 0;

    ssize_t toml_envs_cnt = toml_table_nkval(toml_envs);
    if (toml_envs_cnt <= 0) {
        /* no env entries found in the manifest */
        return 0;
    }

    size_t orig_envs_cnt = 0;
    size_t overwrite_cnt = 0;
    for (const char** orig_env = *envpp; *orig_env; orig_env++, orig_envs_cnt++) {
        char* orig_env_key_end = strchr(*orig_env, '=');
        if (!orig_env_key_end)
            return -PAL_ERROR_INVAL;

        *orig_env_key_end = '\0';
        toml_raw_t toml_env_raw = toml_raw_in(toml_envs, *orig_env);
        if (toml_env_raw) {
            /* found the original-env key in manifest (i.e., loader.env.<key> exists) */
            overwrite_cnt++;
        }
        *orig_env_key_end = '=';
    }

    size_t total_envs_cnt = orig_envs_cnt + toml_envs_cnt - overwrite_cnt;
    const char** new_envp = calloc(total_envs_cnt + 1, sizeof(const char*));
    if (!new_envp)
        return -PAL_ERROR_NOMEM;

    /* For simplicity, allocate each env anew; this is suboptimal but happens only once. First
     * go through original envs and populate new_envp with only those that are not overwritten by
     * manifest envs. Then append all manifest envs to new_envp. */
    size_t idx = 0;
    for (const char** orig_env = *envpp; *orig_env; orig_env++) {
        char* orig_env_key_end = strchr(*orig_env, '=');
        if (!orig_env_key_end)
           return -PAL_ERROR_DENIED;

        *orig_env_key_end = '\0';
        toml_raw_t toml_env_raw = toml_raw_in(toml_envs, *orig_env);
        if (!toml_env_raw) {
            /* this original env is not found in manifest (i.e., not overwritten) */
            *orig_env_key_end = '=';
            new_envp[idx] = malloc_copy(*orig_env, strlen(*orig_env) + 1);
            if (!new_envp[idx]) {
                /* don't care about proper memory cleanup since will terminate anyway */
                return -PAL_ERROR_NOMEM;
            }
            idx++;
        }
        *orig_env_key_end = '=';
    }
    assert(idx < total_envs_cnt);

    for (ssize_t i = 0; i < toml_envs_cnt; i++) {
        const char* toml_env_key = toml_key_in(toml_envs, i);
        if (!toml_env_key)
           return -PAL_ERROR_DENIED;

        toml_raw_t toml_env_value_raw = toml_raw_in(toml_envs, toml_env_key);
        assert(toml_env_value_raw);

        char* toml_env_value = NULL;
        ret = toml_rtos(toml_env_value_raw, &toml_env_value);
        if (ret < 0) {
            /* don't care about proper memory cleanup since will terminate anyway */
            return -PAL_ERROR_NOMEM;
        }

        char* final_env = alloc_concat3(toml_env_key, strlen(toml_env_key), "=", 1, toml_env_value,
                                        strlen(toml_env_value));
        new_envp[idx++] = final_env;
        free(toml_env_value);
    }
    assert(idx == total_envs_cnt);

    *envpp = new_envp;
    return 0;
}

static void configure_logging(void) {
    int ret = 0;
    int log_level = PAL_LOG_DEFAULT_LEVEL;

    char* debug_type = NULL;
    ret = toml_string_in(g_pal_state.manifest_root, "loader.debug_type", &debug_type);
    if (ret < 0)
        INIT_FAIL_MANIFEST(PAL_ERROR_DENIED, "Cannot parse 'loader.debug_type'");
    if (debug_type) {
        free(debug_type);
        INIT_FAIL_MANIFEST(PAL_ERROR_DENIED,
            "'loader.debug_type' has been replaced by 'loader.log_level' and 'loader.log_file'");
    }

    char* log_level_str = NULL;
    ret = toml_string_in(g_pal_state.manifest_root, "loader.log_level", &log_level_str);
    if (ret < 0)
        INIT_FAIL_MANIFEST(PAL_ERROR_DENIED, "Cannot parse 'loader.log_level'");

    if (log_level_str) {
        if (!strcmp(log_level_str, "none")) {
            log_level = LOG_LEVEL_NONE;
        } else if (!strcmp(log_level_str, "error")) {
            log_level = LOG_LEVEL_ERROR;
        } else if (!strcmp(log_level_str, "warning")) {
            log_level = LOG_LEVEL_WARNING;
        } else if (!strcmp(log_level_str, "debug")) {
            log_level = LOG_LEVEL_DEBUG;
        } else if (!strcmp(log_level_str, "trace")) {
            log_level = LOG_LEVEL_TRACE;
        } else if (!strcmp(log_level_str, "all")) {
            log_level = LOG_LEVEL_ALL;
        } else {
            INIT_FAIL_MANIFEST(PAL_ERROR_DENIED, "Unknown 'loader.log_level'");
        }
    }
    free(log_level_str);

    char* log_file = NULL;
    ret = toml_string_in(g_pal_state.manifest_root, "loader.log_file", &log_file);
    if (ret < 0)
        INIT_FAIL_MANIFEST(PAL_ERROR_DENIED, "Cannot parse 'loader.log_file'");

    if (log_file && log_level > LOG_LEVEL_NONE) {
        ret = _DkInitDebugStream(log_file);

        if (ret < 0)
            INIT_FAIL(-ret, "Cannot open log file");
    }
    free(log_file);

    g_pal_control.log_level = log_level;
}

/* Loads a file containing a concatenation of C-strings. The resulting array of pointers is
 * NULL-terminated. All C-strings inside it reside in a single malloc-ed buffer starting at
 * (*res)[0].
 */
static int load_cstring_array(const char* uri, const char*** res) {
    PAL_HANDLE hdl;
    PAL_STREAM_ATTR attr;
    char* buf = NULL;
    const char** array = NULL;
    int ret;

    ret = _DkStreamOpen(&hdl, uri, PAL_ACCESS_RDONLY, 0, 0, 0);
    if (ret < 0)
        return ret;
    ret = _DkStreamAttributesQueryByHandle(hdl, &attr);
    if (ret < 0)
        goto out_fail;
    size_t file_size = attr.pending_size;
    buf = malloc(file_size);
    if (!buf) {
        ret = -PAL_ERROR_NOMEM;
        goto out_fail;
    }
    ret = _DkStreamRead(hdl, 0, file_size, buf, NULL, 0);
    if (ret < 0)
        goto out_fail;
    if (file_size > 0 && buf[file_size - 1] != '\0') {
        ret = -PAL_ERROR_INVAL;
        goto out_fail;
    }

    size_t count = 0;
    for (size_t i = 0; i < file_size; i++)
        if (buf[i] == '\0')
            count++;
    array = malloc(sizeof(char*) * (count + 1));
    if (!array) {
        ret = -PAL_ERROR_NOMEM;
        goto out_fail;
    }
    array[count] = NULL;
    if (file_size > 0) {
        const char** argv_it = array;
        *(argv_it++) = buf;
        for (size_t i = 0; i < file_size - 1; i++)
            if (buf[i] == '\0')
                *(argv_it++) = buf + i + 1;
    }
    *res = array;
    return _DkObjectClose(hdl);

out_fail:
    (void)_DkObjectClose(hdl);
    free(buf);
    free(array);
    return ret;
}

/* 'pal_main' must be called by the host-specific loader.
 * At this point the manifest is assumed to be already parsed, because some PAL loaders use manifest
 * configuration for early initialization.
 */
noreturn void pal_main(uint64_t instance_id,       /* current instance id */
                       PAL_HANDLE parent_process,  /* parent process if it's a child */
                       PAL_HANDLE first_thread,    /* first thread handle */
                       PAL_STR* arguments,         /* application arguments */
                       PAL_STR* environments       /* environment variables */) {
    if (!instance_id) {
        assert(!parent_process);
        if (_DkRandomBitsRead(&instance_id, sizeof(instance_id)) < 0) {
            INIT_FAIL(PAL_ERROR_DENIED, "Could not generate random instance_id");
        }
    }
    g_pal_state.instance_id = instance_id;
    g_pal_state.parent_process = parent_process;

    ssize_t ret;

    assert(g_pal_state.manifest_root);
    assert(g_pal_state.alloc_align && IS_POWER_OF_2(g_pal_state.alloc_align));

    configure_logging();

    char* dummy_exec_str = NULL;
    ret = toml_string_in(g_pal_state.manifest_root, "loader.exec", &dummy_exec_str);
    if (ret < 0 || dummy_exec_str)
        INIT_FAIL(PAL_ERROR_INVAL, "loader.exec is not supported anymore. Please update your "
                                   "manifest according to the current documentation.");
    free(dummy_exec_str);

    bool disable_aslr;
    ret = toml_bool_in(g_pal_state.manifest_root, "loader.insecure__disable_aslr",
                       /*defaultval=*/false, &disable_aslr);
    if (ret < 0) {
        INIT_FAIL_MANIFEST(PAL_ERROR_DENIED, "Cannot parse 'loader.insecure__disable_aslr' "
                                             "(the value must be `true` or `false`)");
    }

    /* Load argv */
    /* TODO: Add an option to specify argv inline in the manifest. This requires an upgrade to the
     * manifest syntax. See https://github.com/oscarlab/graphene/issues/870 (Use YAML or TOML syntax
     * for manifests). 'loader.argv0_override' won't be needed after implementing this feature and
     * resolving https://github.com/oscarlab/graphene/issues/1053 (RFC: graphene invocation).
     */
    bool argv0_overridden = false;
    char* argv0_override = NULL;
    ret = toml_string_in(g_pal_state.manifest_root, "loader.argv0_override", &argv0_override);
    if (ret < 0)
        INIT_FAIL_MANIFEST(PAL_ERROR_DENIED, "Cannot parse 'loader.argv0_override'");

    if (argv0_override) {
        argv0_overridden = true;
        if (!arguments[0]) {
            arguments = malloc(sizeof(const char*) * 2);
            if (!arguments)
                INIT_FAIL(PAL_ERROR_NOMEM, "malloc() failed");
            arguments[1] = NULL;
        }
        arguments[0] = argv0_override;
    }

    bool use_cmdline_argv;
    ret = toml_bool_in(g_pal_state.manifest_root, "loader.insecure__use_cmdline_argv",
                       /*defaultval=*/false, &use_cmdline_argv);
    if (ret < 0) {
        INIT_FAIL_MANIFEST(PAL_ERROR_DENIED, "Cannot parse 'loader.insecure__use_cmdline_argv' "
                                             "(the value must be `true` or `false`)");
    }

    if (use_cmdline_argv) {
        /* Warn only in the first process. */
        if (!parent_process) {
            log_error("Using insecure argv source. Graphene will continue application execution, "
                      "but this configuration must not be used in production!");
        }
    } else {
        char* argv_src_file = NULL;

        ret = toml_string_in(g_pal_state.manifest_root, "loader.argv_src_file", &argv_src_file);
        if (ret < 0)
            INIT_FAIL_MANIFEST(PAL_ERROR_DENIED, "Cannot parse 'loader.argv_src_file'");

        if (argv_src_file) {
            /* Load argv from a file and discard cmdline argv. We trust the file contents (this can
             * be achieved using protected or trusted files). */
            if (arguments[0] && arguments[1])
                log_error("Discarding cmdline arguments (%s %s [...]) because loader.argv_src_file "
                          "was specified in the manifest.", arguments[0], arguments[1]);

            ret = load_cstring_array(argv_src_file, &arguments);
            if (ret < 0)
                INIT_FAIL(-ret, "Cannot load arguments from 'loader.argv_src_file'");

            free(argv_src_file);
        } else if (!argv0_overridden || (arguments[0] && arguments[1])) {
            INIT_FAIL(PAL_ERROR_INVAL, "argv handling wasn't configured in the manifest, but "
                      "cmdline arguments were specified.");
        }
    }

    bool use_host_env;
    ret = toml_bool_in(g_pal_state.manifest_root, "loader.insecure__use_host_env",
                       /*defaultval=*/false, &use_host_env);
    if (ret < 0) {
        INIT_FAIL_MANIFEST(PAL_ERROR_DENIED, "Cannot parse 'loader.insecure__use_host_env' "
                                             "(the value must be `true` or `false`)");
    }

    if (use_host_env) {
        /* Warn only in the first process. */
        if (!parent_process) {
            log_error("Forwarding host environment variables to the app is enabled. Graphene will "
                      "continue application execution, but this configuration must not be used in "
                      "production!");
        }
    } else {
        environments = malloc(sizeof(*environments));
        if (!environments)
            INIT_FAIL(PAL_ERROR_NOMEM, "Out of memory");
        environments[0] = NULL;
    }

    char* env_src_file = NULL;
    ret = toml_string_in(g_pal_state.manifest_root, "loader.env_src_file", &env_src_file);
    if (ret < 0)
        INIT_FAIL_MANIFEST(PAL_ERROR_DENIED, "Cannot parse 'loader.env_src_file'");

    if (use_host_env && env_src_file)
        INIT_FAIL(PAL_ERROR_INVAL, "Wrong manifest configuration - cannot use "
                  "loader.insecure__use_host_env and loader.env_src_file at the same time.");

    if (env_src_file) {
        /* Insert environment variables from a file. We trust the file contents (this can be
         * achieved using protected or trusted files). */
        ret = load_cstring_array(env_src_file, &environments);
        if (ret < 0)
            INIT_FAIL(-ret, "Cannot load environment variables from 'loader.env_src_file'");
        free(env_src_file);
    }


    // TODO: Envs from file should be able to override ones from the manifest, but current
    // code makes this hard to implement.
    ret = insert_envs_from_manifest(&environments);
    if (ret < 0)
        INIT_FAIL(-ret, "Inserting environment variables from the manifest failed");

    load_libraries();

    // TODO: This is just an ugly, temporary hack for PAL regression tests and should only be used
    // there until we clean up the way LibOS is loaded.
    char* entrypoint;
    ret = toml_string_in(g_pal_state.manifest_root, "pal.entrypoint", &entrypoint);
    if (ret < 0)
        INIT_FAIL_MANIFEST(PAL_ERROR_INVAL, "Cannot parse 'pal.entrypoint'");
    if (entrypoint) {
        if (!strstartswith(entrypoint, URI_PREFIX_FILE))
            INIT_FAIL(PAL_ERROR_INVAL, "'pal.entrypoint' is missing 'file:' prefix");
    }

    g_pal_control.host_type       = XSTRINGIFY(HOST_TYPE);
    g_pal_control.manifest_root   = g_pal_state.manifest_root;
    g_pal_control.parent_process  = parent_process;
    g_pal_control.first_thread    = first_thread;
    g_pal_control.disable_aslr    = disable_aslr;

    _DkGetAvailableUserAddressRange(&g_pal_control.user_address.start,
                                    &g_pal_control.user_address.end);

    g_pal_control.alloc_align = g_pal_state.alloc_align;

    if (_DkGetCPUInfo(&g_pal_control.cpu_info) < 0) {
        goto out_fail;
    }
    g_pal_control.mem_info.mem_total = _DkMemoryQuota();

    if (_DkGetTopologyInfo(&g_pal_control.topo_info) < 0) {
        goto out_fail;
    }

    if (entrypoint) {
        // Temporary hack: Assume we're in PAL regression test suite and load the test binary
        // directly, without LibOS.
        if ((ret = load_elf_object(entrypoint, OBJECT_EXEC)) < 0)
            INIT_FAIL(-ret, "Unable to load pal.entrypoint");
    }

    /* Now we will start the execution */
    start_execution(arguments, environments);

out_fail:
    /* We wish we will never reached here */
    INIT_FAIL(PAL_ERROR_DENIED, "unexpected termination");
}
