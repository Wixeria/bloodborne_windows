fn main() {
    if std::env::var("CARGO_CFG_TARGET_OS").as_deref() == Ok("windows")
        && std::env::var_os("CARGO_FEATURE_GUI").is_some()
    {
        let assets = std::path::PathBuf::from(std::env::var_os("CARGO_MANIFEST_DIR").unwrap())
            .join("../../assets/windows");
        let resource = assets.join("bloodborne.rc");
        println!("cargo:rerun-if-changed={}", resource.display());
        println!(
            "cargo:rerun-if-changed={}",
            assets.join("bloodborne.ico").display()
        );
        embed_resource::compile_for(
            resource,
            ["BloodborneLauncher"],
            embed_resource::ParamsIncludeDirs([assets]),
        )
        .manifest_optional()
        .expect("Compile launcher icon");
    }
    if std::env::var("CARGO_CFG_TARGET_OS").as_deref() == Ok("windows")
        && std::env::var("CARGO_CFG_TARGET_ENV").as_deref() == Ok("msvc")
    {
        // The nested native render traversal exceeds the default 1 MiB debug
        // stack on the bindings page. Windows commits this reservation on demand.
        println!("cargo:rustc-link-arg-bin=BloodborneLauncher=/STACK:8388608");
    }
}
