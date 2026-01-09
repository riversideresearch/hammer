project(HAMMER_PARSE C)

file(GLOB_RECURSE APP_SRC_FILES
    "src/*.c"
)

# Create the app module
add_cfe_app(hammer_parse ${APP_SRC_FILES})

# The API to this library (which may be invoked/referenced from other apps)
# is stored in fsw/public_inc.  Using "target_include_directories" is the 
# preferred method of indicating this (vs. directory-scope "include_directories").
target_include_directories(hammer_parse PUBLIC 
  src/backends
  src/parsers
  src)




