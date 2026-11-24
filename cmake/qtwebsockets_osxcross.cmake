# Apple's duplicate-library warning option is unavailable in ld64.lld 18.
cmake_language(DEFER CALL set_property TARGET WebSockets
    PROPERTY QT_NO_DISABLE_WARN_DUPLICATE_LIBRARIES TRUE)
