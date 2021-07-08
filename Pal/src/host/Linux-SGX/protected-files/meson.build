protected_files_dep = declare_dependency(
    sources: [
        'lru_cache.c',
        'protected_files.c',

        'lru_cache.h',
        'protected_files_format.h',
        'protected_files.h',
        'protected_files_internal.h',
    ],

    include_directories: [
        include_directories('.'),
        common_inc,
    ],

    dependencies: uthash_dep,
)
