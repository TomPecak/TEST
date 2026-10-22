//! Silnik trybu standardowego, zachowujący się jak kalkulator Windows 11.
//!
//! - Działania wykonywane od razu, bez kolejności: 2 + 3 × 4 = 20 (po "×" widać "5 ×").
//! - Powtórne "=" powtarza ostatnie działanie: 5 + 3 = = daje 11.
//! - "5 + =" liczy 5 + 5; nowa liczba po "=" i "=" stosuje ostatnie działanie do niej.
//! - %: przy + i − to procent poprzedniej liczby (50 + 10 % -> 50 + 5), przy × i ÷ to x/100.
//! - Działania jednoargumentowe widać w górnej linii: "sqr(5)", "√(9)", "1/(4)", "negate(5)".
//! - Po błędzie (dzielenie przez zero) liczy się dopiero po wyczyszczeniu albo wpisaniu cyfry.
//!
//! Silnik nie wie nic o IPC ani o historii: "=" zwraca gotowe działanie, a zapisuje je wywołujący.

use serde::{Deserialize, Serialize};

use crate::number::{CalcError, Num, NumberFormat, localize};

const MAX_DIGITS: usize = 16;

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub enum Op {
    Add,
    Sub,
    Mul,
    Div,
}

impl Op {
    fn symbol(self) -> &'static str {
        match self {
            Op::Add => "+",
            Op::Sub => "\u{2212}",
            Op::Mul => "\u{d7}",
            Op::Div => "\u{f7}",
        }
    }

    pub fn apply(self, a: &Num, b: &Num) -> Result<Num, CalcError> {
        match self {
            Op::Add => a.add(b),
            Op::Sub => a.sub(b),
            Op::Mul => a.mul(b),
            Op::Div => a.div(b),
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub enum Unary {
    Sqrt,
    Square,
    Reciprocal,
    Negate,
}

impl Unary {
    fn apply(self, v: &Num) -> Result<Num, CalcError> {
        match self {
            Unary::Sqrt => v.sqrt(),
            Unary::Square => v.square(),
            Unary::Reciprocal => v.reciprocal(),
            Unary::Negate => Ok(v.negate()),
        }
    }

    fn wrap(self, inner: &str) -> String {
        match self {
            Unary::Sqrt => format!("\u{221a}({inner})"),
            Unary::Square => format!("sqr({inner})"),
            Unary::Reciprocal => format!("1/({inner})"),
            Unary::Negate => format!("negate({inner})"),
        }
    }
}

/// Liczba w górnej linii: zwykła albo z działaniami jednoargumentowymi
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub enum Operand {
    Num(Num),
    Unary(Unary, Box<Operand>),
}

impl Operand {
    fn format(&self, fmt: &NumberFormat) -> String {
        match self {
            Operand::Num(n) => n.format(fmt),
            Operand::Unary(kind, inner) => kind.wrap(&inner.format(fmt)),
        }
    }
}

/// Element górnej linii
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub enum Part {
    Operand(Operand),
    Op(Op),
    Equals,
}

pub fn format_parts(parts: &[Part], fmt: &NumberFormat) -> String {
    parts
        .iter()
        .map(|part| match part {
            Part::Operand(operand) => operand.format(fmt),
            Part::Op(op) => op.symbol().to_string(),
            Part::Equals => "=".to_string(),
        })
        .collect::<Vec<_>>()
        .join(" ")
}

/// Zakończone działanie (do historii)
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct Calculation {
    pub parts: Vec<Part>,
    pub result: Num,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Key {
    Digit(u8),
    Decimal,
    Op(Op),
    Equals,
    Percent,
    Unary(Unary),
    ClearEntry,
    Clear,
    Backspace,
}

/// Co pokazać na ekranie
#[derive(Clone, Debug, PartialEq)]
pub struct View {
    pub display: String,
    pub expression: String,
    pub error: bool,
    pub value: String,
}

#[derive(Clone, Debug)]
enum Entry {
    /// Liczba wpisywana z klawiatury, w zapisie z kropką: "12.50", "0.", "-3"
    Typing(String),
    /// Wynik albo przywołana wartość: cyfra zaczyna nową liczbę
    Value(Num),
}

#[derive(Clone, Debug)]
pub struct Engine {
    acc: Option<Num>,
    acc_operand: Option<Operand>,
    pending: Option<Op>,
    entry: Entry,
    /// Jak bieżąca liczba wygląda w górnej linii (po działaniu jednoargumentowym, %, przywołaniu)
    operand: Option<Operand>,
    /// Po ostatnim operatorze podano nową liczbę (inaczej kolejny operator tylko podmienia poprzedni)
    started: bool,
    /// Górna linia po "=" (widoczna do następnej czynności)
    finished: Option<Vec<Part>>,
    /// Do powtarzania "="
    last: Option<(Op, Num)>,
    error: Option<(CalcError, Vec<Part>)>,
}

impl Default for Engine {
    fn default() -> Self {
        Self::new()
    }
}

fn digit_count(s: &str) -> usize {
    let digits = s.chars().filter(|c| c.is_ascii_digit()).count();
    // "0,xxx": wiodące zero się nie liczy
    if s.trim_start_matches('-').starts_with("0.") { digits - 1 } else { digits }
}

impl Engine {
    pub fn new() -> Self {
        Self {
            acc: None,
            acc_operand: None,
            pending: None,
            entry: Entry::Typing("0".into()),
            operand: None,
            started: false,
            finished: None,
            last: None,
            error: None,
        }
    }

    pub fn reset(&mut self) {
        *self = Self::new();
    }

    pub fn has_error(&self) -> bool {
        self.error.is_some()
    }

    pub fn current_value(&self) -> Num {
        match &self.entry {
            Entry::Typing(s) => Num::parse(s).unwrap_or_else(Num::zero),
            Entry::Value(v) => v.clone(),
        }
    }

    fn current_operand(&self) -> Operand {
        self.operand.clone().unwrap_or_else(|| Operand::Num(self.current_value()))
    }

    fn typing(&mut self) -> Option<&mut String> {
        match &mut self.entry {
            Entry::Typing(s) if self.started && self.operand.is_none() => Some(s),
            _ => None,
        }
    }

    /// Początek nowego działania po "=": górna linia i oczekujące działanie znikają
    fn leave_finished(&mut self) {
        if self.finished.take().is_some() {
            self.acc = None;
            self.acc_operand = None;
            self.pending = None;
        }
    }

    fn pending_parts(&self) -> Vec<Part> {
        match (self.pending, &self.acc) {
            (Some(op), Some(acc)) => vec![
                Part::Operand(self.acc_operand.clone().unwrap_or_else(|| Operand::Num(acc.clone()))),
                Part::Op(op),
            ],
            _ => Vec::new(),
        }
    }

    fn fail(&mut self, error: CalcError, parts: Vec<Part>) {
        self.reset();
        self.error = Some((error, parts));
    }

    pub fn press(&mut self, key: Key) -> Option<Calculation> {
        match key {
            Key::Digit(d) => self.digit(char::from(b'0' + d.min(9))),
            Key::Decimal => self.decimal(),
            Key::Op(op) => self.binary(op),
            Key::Equals => return self.equals(),
            Key::Percent => self.percent(),
            Key::Unary(Unary::Negate) => self.negate(),
            Key::Unary(kind) => self.unary(kind),
            Key::ClearEntry => self.clear_entry(),
            Key::Clear => self.reset(),
            Key::Backspace => self.backspace(),
        }
        None
    }

    fn digit(&mut self, ch: char) {
        if self.error.is_some() {
            self.reset();
        }
        self.leave_finished();
        if let Some(s) = self.typing() {
            if digit_count(s) >= MAX_DIGITS {
                return;
            }
            match s.as_str() {
                "0" => *s = ch.to_string(),
                "-0" => *s = format!("-{ch}"),
                _ => s.push(ch),
            }
            return;
        }
        self.entry = Entry::Typing(ch.to_string());
        self.operand = None;
        self.started = true;
    }

    fn decimal(&mut self) {
        if self.error.is_some() {
            self.reset();
        }
        self.leave_finished();
        if let Some(s) = self.typing() {
            if !s.contains('.') {
                s.push('.');
            }
            return;
        }
        self.entry = Entry::Typing("0.".into());
        self.operand = None;
        self.started = true;
    }

    fn backspace(&mut self) {
        if self.error.is_some() {
            self.reset();
            return;
        }
        // Po "=" kasuje tylko górną linię
        if self.finished.take().is_some() {
            return;
        }
        if let Some(s) = self.typing() {
            s.pop();
            if s.is_empty() || s == "-" || s == "-0" {
                *s = "0".into();
            }
        }
    }

    fn binary(&mut self, op: Op) {
        if self.error.is_some() {
            return;
        }
        let current = self.current_value();
        if self.finished.take().is_some() {
            self.acc = Some(current.clone());
            self.acc_operand = None;
            self.pending = Some(op);
            self.entry = Entry::Value(current);
            self.operand = None;
            self.started = false;
            return;
        }
        match (self.pending, self.started) {
            (Some(pending), true) => {
                let acc = self.acc.clone().unwrap_or_else(Num::zero);
                match pending.apply(&acc, &current) {
                    Ok(result) => {
                        self.acc = Some(result.clone());
                        self.acc_operand = None;
                        self.entry = Entry::Value(result);
                    }
                    Err(error) => {
                        let mut parts = self.pending_parts();
                        parts.push(Part::Operand(self.current_operand()));
                        parts.push(Part::Op(op));
                        self.fail(error, parts);
                        return;
                    }
                }
            }
            // Drugi operator z rzędu tylko podmienia poprzedni
            (Some(_), false) => {}
            (None, _) => {
                self.acc_operand = self.operand.clone();
                self.acc = Some(current.clone());
                self.entry = Entry::Value(current);
            }
        }
        self.pending = Some(op);
        self.operand = None;
        self.started = false;
    }

    fn equals(&mut self) -> Option<Calculation> {
        if self.error.is_some() {
            self.reset();
            return None;
        }
        let current = self.current_value();
        let current_operand = self.current_operand();
        let (left, left_operand, op, right, right_operand) = if let Some(op) = self.pending {
            let acc = self.acc.clone().unwrap_or_else(Num::zero);
            let acc_operand = self.acc_operand.clone().unwrap_or_else(|| Operand::Num(acc.clone()));
            if self.started {
                (acc, acc_operand, op, current, current_operand)
            } else {
                // "5 + =" liczy 5 + 5
                (acc.clone(), acc_operand, op, acc.clone(), Operand::Num(acc))
            }
        } else if let Some((op, right)) = self.last.clone() {
            (current, current_operand, op, right.clone(), Operand::Num(right))
        } else {
            self.finished = Some(vec![Part::Operand(current_operand), Part::Equals]);
            self.entry = Entry::Value(current);
            self.operand = None;
            self.started = false;
            return None;
        };

        let parts = vec![Part::Operand(left_operand), Part::Op(op), Part::Operand(right_operand), Part::Equals];
        match op.apply(&left, &right) {
            Ok(result) => {
                self.last = Some((op, right));
                self.acc = None;
                self.acc_operand = None;
                self.pending = None;
                self.entry = Entry::Value(result.clone());
                self.operand = None;
                self.started = false;
                self.finished = Some(parts.clone());
                Some(Calculation { parts, result })
            }
            Err(error) => {
                self.fail(error, parts);
                None
            }
        }
    }

    fn unary(&mut self, kind: Unary) {
        if self.error.is_some() {
            return;
        }
        let current = self.current_value();
        let base = if self.finished.take().is_some() { Operand::Num(current.clone()) } else { self.current_operand() };
        let wrapped = Operand::Unary(kind, Box::new(base));
        match kind.apply(&current) {
            Ok(value) => {
                self.entry = Entry::Value(value);
                self.operand = Some(wrapped);
                self.started = true;
            }
            Err(error) => {
                let mut parts = self.pending_parts();
                parts.push(Part::Operand(wrapped));
                self.fail(error, parts);
            }
        }
    }

    fn negate(&mut self) {
        if self.error.is_some() {
            return;
        }
        // W trakcie wpisywania tylko zmienia znak liczby
        if self.finished.is_none() {
            if let Some(s) = self.typing() {
                if let Some(rest) = s.strip_prefix('-') {
                    *s = rest.to_string();
                } else if s != "0" {
                    s.insert(0, '-');
                }
                return;
            }
        }
        self.unary(Unary::Negate);
    }

    fn percent(&mut self) {
        if self.error.is_some() {
            return;
        }
        self.leave_finished();
        let current = self.current_value();
        let hundred = Num::from_i64(100);
        let value = match (self.pending, &self.acc) {
            (Some(Op::Add | Op::Sub), Some(acc)) => acc.mul(&current).and_then(|v| v.div(&hundred)),
            (Some(Op::Mul | Op::Div), _) => current.div(&hundred),
            _ => Ok(Num::zero()),
        };
        match value {
            Ok(value) => {
                self.operand = Some(Operand::Num(value.clone()));
                self.entry = Entry::Value(value);
                self.started = true;
            }
            Err(error) => {
                let parts = self.pending_parts();
                self.fail(error, parts);
            }
        }
    }

    fn clear_entry(&mut self) {
        if self.error.is_some() || self.finished.is_some() {
            self.reset();
            return;
        }
        self.entry = Entry::Typing("0".into());
        self.operand = None;
        self.started = true;
    }

    /// Wartość z pamięci, historii albo schowka staje się bieżącą liczbą
    pub fn set_value(&mut self, value: Num) {
        if self.error.is_some() {
            self.reset();
        }
        self.leave_finished();
        self.entry = Entry::Value(value);
        self.operand = None;
        self.started = true;
    }

    /// Po MS / M+ / M−: liczba zostaje, ale następna cyfra zaczyna nową
    pub fn commit(&mut self) {
        if self.error.is_none() {
            self.entry = Entry::Value(self.current_value());
        }
    }

    /// Kliknięcie w historię: jej działanie w górnej linii, wynik na wyświetlaczu
    pub fn show_calculation(&mut self, calculation: &Calculation) {
        self.reset();
        self.entry = Entry::Value(calculation.result.clone());
        self.finished = Some(calculation.parts.clone());
    }

    pub fn view(&self, fmt: &NumberFormat) -> View {
        if let Some((error, parts)) = &self.error {
            return View {
                display: error.message().into(),
                expression: format_parts(parts, fmt),
                error: true,
                value: String::new(),
            };
        }
        let display = match &self.entry {
            Entry::Typing(s) => localize(s, fmt),
            Entry::Value(v) => v.format(fmt),
        };
        let expression = match &self.finished {
            Some(parts) => format_parts(parts, fmt),
            None => {
                let mut parts = self.pending_parts();
                if let Some(operand) = &self.operand {
                    parts.push(Part::Operand(operand.clone()));
                }
                format_parts(&parts, fmt)
            }
        };
        View { display, expression, error: false, value: self.current_value().canonical() }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Klawisze jak na klawiaturze: "12+3*4=" , "r" = 1/x, "q" = sqr, "@" = √, "n" = ±, "%" ,
    /// "c" = C, "e" = CE, "<" = backspace
    fn run(keys: &str) -> (Engine, Vec<Calculation>) {
        let mut engine = Engine::new();
        let mut history = Vec::new();
        for ch in keys.chars() {
            let key = match ch {
                '0'..='9' => Key::Digit(ch as u8 - b'0'),
                '.' => Key::Decimal,
                '+' => Key::Op(Op::Add),
                '-' => Key::Op(Op::Sub),
                '*' => Key::Op(Op::Mul),
                '/' => Key::Op(Op::Div),
                '=' => Key::Equals,
                '%' => Key::Percent,
                '@' => Key::Unary(Unary::Sqrt),
                'q' => Key::Unary(Unary::Square),
                'r' => Key::Unary(Unary::Reciprocal),
                'n' => Key::Unary(Unary::Negate),
                'c' => Key::Clear,
                'e' => Key::ClearEntry,
                '<' => Key::Backspace,
                _ => panic!("unknown key {ch}"),
            };
            if let Some(calculation) = engine.press(key) {
                history.push(calculation);
            }
        }
        (engine, history)
    }

    fn view(keys: &str) -> View {
        run(keys).0.view(&NumberFormat::default())
    }

    fn display(keys: &str) -> String {
        view(keys).display
    }

    #[test]
    fn typing() {
        assert_eq!(display(""), "0");
        assert_eq!(display("0012"), "12");
        assert_eq!(display("1234567"), "1,234,567");
        assert_eq!(display("1.50"), "1.50");
        assert_eq!(display("1..5"), "1.5");
        assert_eq!(display(".5"), "0.5");
        assert_eq!(display("12345678901234567890"), "1,234,567,890,123,456");
        assert_eq!(display("123<"), "12");
        assert_eq!(display("1<<"), "0");
        assert_eq!(display("5n"), "-5");
        assert_eq!(display("5nn"), "5");
    }

    #[test]
    fn immediate_execution() {
        assert_eq!(display("2+3*4="), "20");
        assert_eq!(view("2+3*").expression, "5 \u{d7}");
        assert_eq!(view("2+3*").display, "5");
        assert_eq!(view("12+7=").expression, "12 + 7 =");
        assert_eq!(display("0.1+0.2="), "0.3");
        assert_eq!(display("10/4="), "2.5");
    }

    #[test]
    fn operators_and_equals() {
        assert_eq!(display("5+-3="), "2"); // drugi operator podmienia pierwszy
        assert_eq!(display("5+="), "10");
        assert_eq!(display("5+3=="), "11");
        assert_eq!(display("5+3=2="), "5"); // nowa liczba po "=" i ostatnie działanie
        assert_eq!(view("5+3==").expression, "8 + 3 =");
        assert_eq!(display("5+3=*2="), "16"); // wynik jako początek nowego działania
        assert_eq!(view("5=").expression, "5 =");
    }

    #[test]
    fn percent() {
        assert_eq!(display("50+10%"), "5");
        assert_eq!(display("50+10%="), "55");
        assert_eq!(display("200*15%="), "30");
        assert_eq!(display("15%"), "0");
    }

    #[test]
    fn unary_operations() {
        assert_eq!(display("9@"), "3");
        assert_eq!(view("9@").expression, "\u{221a}(9)");
        assert_eq!(view("5qq").expression, "sqr(sqr(5))");
        assert_eq!(display("5qq"), "625");
        assert_eq!(view("2+9@").expression, "2 + \u{221a}(9)");
        assert_eq!(display("2+9@="), "5");
        assert_eq!(display("4r"), "0.25");
        assert_eq!(view("5+3=n").expression, "negate(8)");
        assert_eq!(display("2@"), "1.414213562373095");
    }

    #[test]
    fn errors() {
        let v = view("5/0=");
        assert!(v.error);
        assert_eq!(v.display, "Cannot divide by zero");
        assert_eq!(v.expression, "5 \u{f7} 0 =");
        assert_eq!(display("0/0="), "Result is undefined");
        assert_eq!(display("4n@"), "Invalid input");
        assert_eq!(display("0r"), "Cannot divide by zero");
        // operatory nie działają w stanie błędu, cyfra zaczyna od nowa
        assert_eq!(display("5/0=+"), "Cannot divide by zero");
        assert_eq!(display("5/0=7"), "7");
        assert_eq!(display("5/0=c"), "0");
    }

    #[test]
    fn clear_entry_and_clear() {
        assert_eq!(display("5+3e="), "5");
        assert_eq!(display("5+3e2="), "7");
        assert_eq!(view("5+3e").expression, "5 +");
        assert_eq!(display("5+3c"), "0");
        assert_eq!(view("5+3c").expression, "");
    }

    #[test]
    fn history_entries() {
        let (_, history) = run("2+3=*4=c1-1=");
        let fmt = NumberFormat::default();
        let lines: Vec<String> = history
            .iter()
            .map(|c| format!("{} {}", format_parts(&c.parts, &fmt), c.result.format(&fmt)))
            .collect();
        assert_eq!(lines, vec!["2 + 3 = 5", "5 \u{d7} 4 = 20", "1 \u{2212} 1 = 0"]);
    }

    #[test]
    fn values_from_outside() {
        let mut engine = Engine::new();
        engine.press(Key::Digit(5));
        engine.press(Key::Op(Op::Add));
        engine.set_value(Num::parse("2.5").unwrap());
        engine.press(Key::Equals);
        assert_eq!(engine.view(&NumberFormat::default()).display, "7.5");
        // po MS liczba zostaje, ale cyfra zaczyna nową
        engine.commit();
        engine.press(Key::Digit(9));
        assert_eq!(engine.view(&NumberFormat::default()).display, "9");
    }
}
