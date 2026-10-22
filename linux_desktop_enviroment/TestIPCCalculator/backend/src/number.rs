//! Liczby kalkulatora: dziesiętne o stałej precyzji (34 cyfry znaczące) i ich formatowanie.
//!
//! Arytmetyka dziesiętna (bigdecimal) zamiast binarnej: 0,1 + 0,2 = 0,3. Po każdym działaniu wynik
//! jest zaokrąglany do 34 cyfr znaczących (jak decimal128), a wyświetlany z 16 cyframi, tak jak
//! kalkulator Windows w trybie standardowym.

use std::str::FromStr;

use bigdecimal::{BigDecimal, RoundingMode, Signed, Zero};
use serde::{Deserialize, Deserializer, Serialize, Serializer};

pub const PRECISION: i64 = 34;
pub const DISPLAY_DIGITS: i64 = 16;
const MAX_EXPONENT: i64 = 9999;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CalcError {
    DivideByZero,
    Undefined,
    InvalidInput,
    Overflow,
}

impl CalcError {
    pub fn message(self) -> &'static str {
        match self {
            CalcError::DivideByZero => "Cannot divide by zero",
            CalcError::Undefined => "Result is undefined",
            CalcError::InvalidInput => "Invalid input",
            CalcError::Overflow => "Overflow",
        }
    }
}

/// Separatory z ustawień regionalnych klienta
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct NumberFormat {
    pub decimal: String,
    pub group: String,
}

impl Default for NumberFormat {
    fn default() -> Self {
        Self { decimal: ".".into(), group: ",".into() }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct Num(BigDecimal);

/// Wykładnik pierwszej cyfry znaczącej: 123,4 -> 2; 0,05 -> -2
fn exponent(v: &BigDecimal) -> i64 {
    let (int, scale) = v.as_bigint_and_exponent();
    int.magnitude().to_string().len() as i64 - 1 - scale
}

fn round_significant(v: &BigDecimal, digits: i64, mode: RoundingMode) -> BigDecimal {
    if v.is_zero() {
        return BigDecimal::zero();
    }
    v.with_scale_round(digits - 1 - exponent(v), mode).normalized()
}

/// Zapis bez wykładnika: cyfry i skala -> "-1234.5"
fn plain(v: &BigDecimal) -> String {
    let (int, scale) = v.normalized().as_bigint_and_exponent();
    let digits = int.magnitude().to_string();
    let sign = if int.is_negative() { "-" } else { "" };
    if scale <= 0 {
        return format!("{sign}{digits}{}", "0".repeat((-scale) as usize));
    }
    let scale = scale as usize;
    if digits.len() > scale {
        let (whole, frac) = digits.split_at(digits.len() - scale);
        format!("{sign}{whole}.{frac}")
    } else {
        format!("{sign}0.{}{digits}", "0".repeat(scale - digits.len()))
    }
}

/// Grupowanie części całkowitej po 3 cyfry: "1234567" -> "1 234 567"
fn group_digits(whole: &str, separator: &str) -> String {
    if separator.is_empty() || whole.len() <= 3 {
        return whole.to_string();
    }
    let mut out = String::new();
    for (i, ch) in whole.chars().enumerate() {
        if i > 0 && (whole.len() - i) % 3 == 0 {
            out.push_str(separator);
        }
        out.push(ch);
    }
    out
}

/// Liczba w zapisie z kropką ("-1234.50", "0.") -> z separatorami klienta
pub fn localize(text: &str, fmt: &NumberFormat) -> String {
    let (sign, rest) = match text.strip_prefix('-') {
        Some(rest) => ("-", rest),
        None => ("", text),
    };
    match rest.split_once('.') {
        Some((whole, frac)) => format!("{sign}{}{}{frac}", group_digits(whole, &fmt.group), fmt.decimal),
        None => format!("{sign}{}", group_digits(rest, &fmt.group)),
    }
}

impl Num {
    pub fn zero() -> Self {
        Num(BigDecimal::zero())
    }

    pub fn from_i64(v: i64) -> Self {
        Num(BigDecimal::from(v))
    }

    /// Wynik działania: zaokrąglenie do precyzji i kontrola zakresu
    fn checked(v: BigDecimal) -> Result<Num, CalcError> {
        if v.is_zero() {
            return Ok(Num::zero());
        }
        let v = round_significant(&v, PRECISION, RoundingMode::HalfEven);
        let e = exponent(&v);
        if e > MAX_EXPONENT {
            return Err(CalcError::Overflow);
        }
        if e < -MAX_EXPONENT {
            return Ok(Num::zero());
        }
        Ok(Num(v))
    }

    /// Liczba w zapisie z kropką, opcjonalnie z wykładnikiem ("1.5", "-2e-3", "12.")
    pub fn parse(text: &str) -> Option<Num> {
        let text = text.trim().trim_end_matches('.');
        let valid = !text.is_empty()
            && text.chars().all(|c| c.is_ascii_digit() || matches!(c, '.' | '-' | '+' | 'e' | 'E'))
            && text.chars().any(|c| c.is_ascii_digit());
        if !valid {
            return None;
        }
        BigDecimal::from_str(text).ok().and_then(|v| Num::checked(v).ok())
    }

    /// Tekst wpisany lub wklejony przez użytkownika, z jego separatorami ("1 234,5" albo "1,234.5")
    pub fn parse_localized(text: &str, fmt: &NumberFormat) -> Option<Num> {
        let mut text: String = text.chars().filter(|c| !c.is_whitespace() && *c != '\u{a0}' && *c != '\u{202f}').collect();
        text = text.replace('\u{2212}', "-");
        if !fmt.group.trim().is_empty() && fmt.group != fmt.decimal {
            text = text.replace(fmt.group.trim(), "");
        }
        if fmt.decimal != "." {
            text = text.replace(&fmt.decimal, ".");
        }
        Num::parse(&text)
    }

    pub fn is_zero(&self) -> bool {
        self.0.is_zero()
    }

    pub fn is_negative(&self) -> bool {
        self.0.is_negative()
    }

    pub fn add(&self, o: &Num) -> Result<Num, CalcError> {
        Num::checked(&self.0 + &o.0)
    }

    pub fn sub(&self, o: &Num) -> Result<Num, CalcError> {
        Num::checked(&self.0 - &o.0)
    }

    pub fn mul(&self, o: &Num) -> Result<Num, CalcError> {
        Num::checked(&self.0 * &o.0)
    }

    pub fn div(&self, o: &Num) -> Result<Num, CalcError> {
        if o.is_zero() {
            return Err(if self.is_zero() { CalcError::Undefined } else { CalcError::DivideByZero });
        }
        Num::checked(&self.0 / &o.0)
    }

    pub fn negate(&self) -> Num {
        Num(-&self.0)
    }

    pub fn square(&self) -> Result<Num, CalcError> {
        self.mul(self)
    }

    pub fn sqrt(&self) -> Result<Num, CalcError> {
        if self.is_negative() {
            return Err(CalcError::InvalidInput);
        }
        self.0.sqrt().ok_or(CalcError::InvalidInput).and_then(Num::checked)
    }

    pub fn reciprocal(&self) -> Result<Num, CalcError> {
        Num::from_i64(1).div(self)
    }

    /// Potęga o wykładniku całkowitym (szybkie potęgowanie, z kontrolą zakresu na każdym kroku)
    pub fn powi(&self, mut n: i64) -> Result<Num, CalcError> {
        if n == 0 {
            return if self.is_zero() { Err(CalcError::Undefined) } else { Ok(Num::from_i64(1)) };
        }
        let negative = n < 0;
        n = n.abs();
        let mut base = self.clone();
        let mut result = Num::from_i64(1);
        while n > 0 {
            if n & 1 == 1 {
                result = result.mul(&base)?;
            }
            n >>= 1;
            if n > 0 {
                base = base.mul(&base)?;
            }
        }
        if negative { result.reciprocal() } else { Ok(result) }
    }

    /// Wartość całkowita (dla wykładników), None gdy ma część ułamkową albo jest za duża
    pub fn to_integer(&self) -> Option<i64> {
        let v = self.0.normalized();
        let (_, scale) = v.as_bigint_and_exponent();
        if scale > 0 || exponent(&v) > 17 {
            return None;
        }
        plain(&v).parse().ok()
    }

    /// Zapis kanoniczny: kropka, bez grupowania, pełna precyzja; bardzo duże i małe z wykładnikiem
    pub fn canonical(&self) -> String {
        if self.is_zero() {
            return "0".into();
        }
        let e = exponent(&self.0);
        if (-20..=40).contains(&e) {
            return plain(&self.0);
        }
        self.scientific(PRECISION, &NumberFormat { decimal: ".".into(), group: String::new() })
    }

    fn scientific(&self, digits: i64, fmt: &NumberFormat) -> String {
        let rounded = round_significant(&self.0, digits, RoundingMode::HalfUp);
        let e = exponent(&rounded);
        let (int, _) = rounded.as_bigint_and_exponent();
        let all = int.magnitude().to_string();
        let mantissa = all.trim_end_matches('0');
        let mantissa = if mantissa.is_empty() { "0" } else { mantissa };
        let sign = if rounded.is_negative() { "-" } else { "" };
        let body = if mantissa.len() > 1 {
            format!("{}{}{}", &mantissa[..1], fmt.decimal, &mantissa[1..])
        } else {
            mantissa.to_string()
        };
        format!("{sign}{body}e{}{}", if e < 0 { "-" } else { "+" }, e.abs())
    }

    /// Do wyświetlenia: 16 cyfr, separatory klienta; bardzo duże i małe w zapisie naukowym
    pub fn format(&self, fmt: &NumberFormat) -> String {
        if self.is_zero() {
            return "0".into();
        }
        let e = exponent(&self.0);
        if e >= 0 {
            let rounded = round_significant(&self.0, DISPLAY_DIGITS, RoundingMode::HalfUp);
            if exponent(&rounded) >= DISPLAY_DIGITS {
                return self.scientific(DISPLAY_DIGITS, fmt);
            }
            return localize(&plain(&rounded), fmt);
        }
        if e <= -DISPLAY_DIGITS {
            return self.scientific(DISPLAY_DIGITS, fmt);
        }
        // Ułamki: najwyżej 16 miejsc po przecinku (1/30 = 0,0333333333333333)
        let rounded = self.0.with_scale_round(DISPLAY_DIGITS, RoundingMode::HalfUp).normalized();
        if rounded.is_zero() {
            return self.scientific(DISPLAY_DIGITS, fmt);
        }
        localize(&plain(&rounded), fmt)
    }
}

// W pliku stanu liczby są zapisane jako tekst kanoniczny
impl Serialize for Num {
    fn serialize<S: Serializer>(&self, s: S) -> Result<S::Ok, S::Error> {
        s.serialize_str(&self.canonical())
    }
}

impl<'de> Deserialize<'de> for Num {
    fn deserialize<D: Deserializer<'de>>(d: D) -> Result<Self, D::Error> {
        let text = String::deserialize(d)?;
        Num::parse(&text).ok_or_else(|| serde::de::Error::custom(format!("not a number: {text}")))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn n(s: &str) -> Num {
        Num::parse(s).unwrap()
    }

    fn pl() -> NumberFormat {
        NumberFormat { decimal: ",".into(), group: "\u{a0}".into() }
    }

    #[test]
    fn decimal_arithmetic_is_exact() {
        assert_eq!(n("0.1").add(&n("0.2")).unwrap().canonical(), "0.3");
        assert_eq!(n("1.1").mul(&n("1.1")).unwrap().canonical(), "1.21");
        assert_eq!(n("10").sub(&n("0.01")).unwrap().canonical(), "9.99");
    }

    #[test]
    fn division_and_errors() {
        assert_eq!(n("1").div(&n("3")).unwrap().format(&NumberFormat::default()), "0.3333333333333333");
        assert_eq!(n("2").div(&n("3")).unwrap().format(&NumberFormat::default()), "0.6666666666666667");
        assert_eq!(n("1").div(&n("30")).unwrap().format(&NumberFormat::default()), "0.0333333333333333");
        assert_eq!(n("1").div(&n("0")), Err(CalcError::DivideByZero));
        assert_eq!(n("0").div(&n("0")), Err(CalcError::Undefined));
        assert_eq!(n("-4").sqrt(), Err(CalcError::InvalidInput));
        assert_eq!(n("16").sqrt().unwrap().canonical(), "4");
    }

    #[test]
    fn display_format() {
        let fmt = NumberFormat::default();
        assert_eq!(n("1234567.5").format(&fmt), "1,234,567.5");
        assert_eq!(n("1234567.5").format(&pl()), "1\u{a0}234\u{a0}567,5");
        assert_eq!(n("-0.5").format(&pl()), "-0,5");
        assert_eq!(n("9999999999999999").format(&fmt), "9,999,999,999,999,999");
        assert_eq!(n("99999999999999999").format(&fmt), "1e+17");
        assert_eq!(n("12345678901234567890").format(&fmt), "1.234567890123457e+19");
        assert_eq!(n("0.00000000000000001").format(&pl()), "1e-17");
        assert_eq!(n("2").sqrt().unwrap().format(&fmt), "1.414213562373095");
    }

    #[test]
    fn parsing_localized_text() {
        assert_eq!(Num::parse_localized("1 234,5", &pl()).unwrap().canonical(), "1234.5");
        assert_eq!(Num::parse_localized("1,234.5", &NumberFormat::default()).unwrap().canonical(), "1234.5");
        assert_eq!(Num::parse_localized("−3,25", &pl()).unwrap().canonical(), "-3.25");
        assert!(Num::parse_localized("abc", &pl()).is_none());
    }

    #[test]
    fn powers_and_range() {
        assert_eq!(n("2").powi(10).unwrap().canonical(), "1024");
        assert_eq!(n("2").powi(-2).unwrap().canonical(), "0.25");
        assert_eq!(n("10").powi(10000), Err(CalcError::Overflow));
        assert_eq!(n("1e9999").mul(&n("10")), Err(CalcError::Overflow));
    }
}
