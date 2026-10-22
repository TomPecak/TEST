// Generuje kod Rusta z kontraktu ../contract/calculator.capnp (wymaga kompilatora `capnp` w PATH)
fn main() {
    println!("cargo:rerun-if-changed=../contract/calculator.capnp");
    capnpc::CompilerCommand::new()
        .src_prefix("../contract")
        .file("../contract/calculator.capnp")
        .run()
        .expect("kompilacja schematu calculator.capnp");
}
