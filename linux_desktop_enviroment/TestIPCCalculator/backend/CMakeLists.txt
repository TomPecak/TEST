# The calculator service (Rust). Cargo builds it; build.rs generates the Rust code of the contract
# (../contract/calculator.capnp). Here it only becomes a target of the whole build.
include(MaiaRust)

maia_add_cargo_executable(calculator-service
    MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/test-ipc-calculator
)
