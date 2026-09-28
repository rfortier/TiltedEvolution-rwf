target("SkyrimTogetherSKSE")
    set_basename("SkyrimTogetherSKSE")
    set_kind("shared")
    set_group("Client")
    set_symbols("debug", "hidden")

    add_includedirs(".")
    add_headerfiles("**.h")
    add_files("**.cpp")

    -- deliberately dependency free: only system libs, so the OS loader can
    -- always resolve this dll before the payload has been deployed
    add_syslinks("user32", "version")
    add_ldflags("/INCREMENTAL:NO", { force = true })
