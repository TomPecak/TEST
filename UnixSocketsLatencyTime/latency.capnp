@0xdbb9ad1f14bf0b36;

# Definiujemy interfejs RPC (Usługę)
interface PingService {
  # Definiujemy metodę "ping", która przyjmuje strukturę z polem 'seq' 
  # i zwraca strukturę z polem 'seq'.
  ping @0 (seq :UInt64) -> (seq :UInt64);
}
