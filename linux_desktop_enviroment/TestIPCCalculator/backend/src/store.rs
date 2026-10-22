//! Historia i pamięć kalkulatora: wspólne dla wszystkich sesji, zapisywane na dysku
//! ($XDG_DATA_HOME/test-ipc-calculator/state.json), więc przeżywają restart okna i usługi.
//!
//! Pamięć działa jak w kalkulatorze Windows: MS dokłada nową pozycję na początek listy,
//! M+ / M− zmieniają najnowszą, MR przywołuje najnowszą; każdą pozycję można też zmienić osobno.

use std::fs;
use std::path::PathBuf;

use serde::{Deserialize, Serialize};

use crate::engine::Calculation;
use crate::number::{CalcError, Num};

const HISTORY_LIMIT: usize = 100;

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct HistoryEntry {
    pub id: u64,
    pub calculation: Calculation,
}

#[derive(Debug, Default, Serialize, Deserialize)]
struct State {
    history: Vec<HistoryEntry>,
    memory: Vec<Num>,
    next_id: u64,
}

pub enum MemoryAction {
    Recall,
    Add,
    Subtract,
    Clear,
}

pub struct Store {
    state: State,
    path: Option<PathBuf>,
}

impl Store {
    /// Stan z dysku (albo pusty, gdy pliku nie ma lub jest uszkodzony)
    pub fn load(path: Option<PathBuf>) -> Self {
        let state = path
            .as_ref()
            .and_then(|p| fs::read_to_string(p).ok())
            .and_then(|text| serde_json::from_str(&text).ok())
            .unwrap_or_default();
        Self { state, path }
    }

    /// Zapis atomowy: plik tymczasowy obok i zmiana nazwy
    fn save(&self) {
        let Some(path) = &self.path else { return };
        let Ok(text) = serde_json::to_string_pretty(&self.state) else { return };
        if let Some(dir) = path.parent() {
            let _ = fs::create_dir_all(dir);
        }
        let temporary = path.with_extension("json.tmp");
        if fs::write(&temporary, text).is_ok() {
            let _ = fs::rename(&temporary, path);
        }
    }

    pub fn history(&self) -> &[HistoryEntry] {
        &self.state.history
    }

    pub fn memory(&self) -> &[Num] {
        &self.state.memory
    }

    pub fn find_history(&self, id: u64) -> Option<&Calculation> {
        self.state.history.iter().find(|e| e.id == id).map(|e| &e.calculation)
    }

    pub fn add_history(&mut self, calculation: Calculation) {
        self.state.next_id += 1;
        let id = self.state.next_id;
        self.state.history.insert(0, HistoryEntry { id, calculation });
        self.state.history.truncate(HISTORY_LIMIT);
        self.save();
    }

    pub fn clear_history(&mut self) {
        self.state.history.clear();
        self.save();
    }

    pub fn memory_store(&mut self, value: Num) {
        self.state.memory.insert(0, value);
        self.save();
    }

    pub fn memory_clear(&mut self) {
        self.state.memory.clear();
        self.save();
    }

    pub fn memory_top(&self) -> Option<Num> {
        self.state.memory.first().cloned()
    }

    /// M+ / M− na najnowszej pozycji (pusta pamięć: nowa pozycja)
    pub fn memory_add_top(&mut self, value: &Num, subtract: bool) -> Result<(), CalcError> {
        if self.state.memory.is_empty() {
            self.memory_store(if subtract { value.negate() } else { value.clone() });
            return Ok(());
        }
        self.memory_item(0, MemoryAction::Add.with_sign(subtract), value).map(|_| ())
    }

    /// Działanie na pozycji pamięci; Recall zwraca jej wartość
    pub fn memory_item(&mut self, index: usize, action: MemoryAction, value: &Num) -> Result<Option<Num>, CalcError> {
        let Some(item) = self.state.memory.get(index).cloned() else { return Ok(None) };
        match action {
            MemoryAction::Recall => return Ok(Some(item)),
            MemoryAction::Add => self.state.memory[index] = item.add(value)?,
            MemoryAction::Subtract => self.state.memory[index] = item.sub(value)?,
            MemoryAction::Clear => {
                self.state.memory.remove(index);
            }
        }
        self.save();
        Ok(None)
    }
}

impl MemoryAction {
    fn with_sign(self, subtract: bool) -> MemoryAction {
        if subtract { MemoryAction::Subtract } else { self }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::engine::{Engine, Key, Op};

    fn n(s: &str) -> Num {
        Num::parse(s).unwrap()
    }

    #[test]
    fn memory_like_windows() {
        let mut store = Store::load(None);
        store.memory_add_top(&n("5"), false).unwrap(); // M+ na pustej: nowa pozycja 5
        store.memory_store(n("2")); // MS: nowa pozycja na początku
        store.memory_add_top(&n("3"), false).unwrap(); // M+ zmienia najnowszą: 5
        store.memory_add_top(&n("1"), true).unwrap(); // M−: 4
        let values: Vec<String> = store.memory().iter().map(|v| v.canonical()).collect();
        assert_eq!(values, vec!["4", "5"]);
        assert_eq!(store.memory_item(1, MemoryAction::Recall, &Num::zero()).unwrap().unwrap().canonical(), "5");
        store.memory_item(0, MemoryAction::Clear, &Num::zero()).unwrap();
        assert_eq!(store.memory().len(), 1);
    }

    #[test]
    fn history_is_saved_and_loaded() {
        let dir = std::env::temp_dir().join(format!("calc-store-test-{}", std::process::id()));
        let path = dir.join("state.json");
        {
            let mut store = Store::load(Some(path.clone()));
            let mut engine = Engine::new();
            for key in [Key::Digit(2), Key::Op(Op::Add), Key::Digit(3), Key::Equals] {
                if let Some(calculation) = engine.press(key) {
                    store.add_history(calculation);
                }
            }
            store.memory_store(n("0.1"));
        }
        let store = Store::load(Some(path.clone()));
        assert_eq!(store.history().len(), 1);
        assert_eq!(store.history()[0].calculation.result.canonical(), "5");
        assert_eq!(store.memory()[0].canonical(), "0.1");
        let _ = fs::remove_dir_all(dir);
    }
}
