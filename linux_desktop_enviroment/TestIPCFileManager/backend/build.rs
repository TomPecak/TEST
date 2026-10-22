// Generuje kod Rusta z kontraktu ../contract/files.capnp (wymaga kompilatora `capnp` w PATH)
fn main() {
    println!("cargo:rerun-if-changed=../contract/files.capnp");
    capnpc::CompilerCommand::new()
        .src_prefix("../contract")
        .file("../contract/files.capnp")
        .run()
        .expect("kompilacja schematu files.capnp");
}
