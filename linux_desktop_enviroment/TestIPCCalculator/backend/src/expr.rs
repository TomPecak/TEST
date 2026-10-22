//! Wyrażenia z kolejnością działań, dla widżetów, wyszukiwarki w menu Start i agentów AI:
//! "2+3*(4-1)", "2^10/1,5", "√16 + 50%", "-(3-5)*2", "1 234,5 × 2".
//!
//! Gramatyka (zejście rekurencyjne):
//!   expr    = term (("+" | "-") term)*
//!   term    = unary (("*" | "/") unary)*
//!   unary   = ("-" | "+") unary | power
//!   power   = postfix ("^" unary)?              (prawostronnie łączne: 2^3^2 = 2^9)
//!   postfix = primary "%"*                     (x% = x/100)
//!   primary = liczba | "(" expr ")" | ("√" | "sqrt") primary

use crate::number::{CalcError, Num, NumberFormat};

#[derive(Clone, Debug, PartialEq)]
enum Token {
    Num(Num),
    Plus,
    Minus,
    Times,
    Divide,
    Power,
    Percent,
    Open,
    Close,
    Sqrt,
}

#[derive(Debug, PartialEq)]
pub enum ExprError {
    Syntax(String),
    Math(CalcError),
}

impl ExprError {
    pub fn message(&self) -> String {
        match self {
            ExprError::Syntax(text) => text.clone(),
            ExprError::Math(error) => error.message().to_string(),
        }
    }
}

impl From<CalcError> for ExprError {
    fn from(error: CalcError) -> Self {
        ExprError::Math(error)
    }
}

fn tokenize(text: &str, fmt: &NumberFormat) -> Result<Vec<Token>, ExprError> {
    let chars: Vec<char> = text.chars().collect();
    let mut tokens = Vec::new();
    let mut i = 0;
    let decimal = fmt.decimal.chars().next().unwrap_or('.');
    let group = fmt.group.chars().next();
    while i < chars.len() {
        let c = chars[i];
        let single = match c {
            ' ' | '\u{a0}' | '\u{202f}' | '\t' => None,
            '+' => Some(Token::Plus),
            '-' | '\u{2212}' => Some(Token::Minus),
            '*' | '\u{d7}' | '\u{b7}' => Some(Token::Times),
            '/' | '\u{f7}' | ':' => Some(Token::Divide),
            '^' => Some(Token::Power),
            '%' => Some(Token::Percent),
            '(' => Some(Token::Open),
            ')' => Some(Token::Close),
            '\u{221a}' => Some(Token::Sqrt),
            _ => {
                if c.is_ascii_digit() || c == '.' || c == decimal {
                    // Liczba: cyfry, separator dziesiętny, separator tysięcy klienta (pomijany)
                    let mut number = String::new();
                    while i < chars.len() {
                        let d = chars[i];
                        if d.is_ascii_digit() {
                            number.push(d);
                        } else if d == '.' || d == decimal {
                            number.push('.');
                        } else if Some(d) == group && d != decimal && chars.get(i + 1).is_some_and(|n| n.is_ascii_digit()) {
                            // separator tysięcy w środku liczby
                        } else {
                            break;
                        }
                        i += 1;
                    }
                    let value = Num::parse(&number).ok_or_else(|| ExprError::Syntax(format!("Invalid number “{number}”")))?;
                    tokens.push(Token::Num(value));
                    continue;
                }
                let rest: String = chars[i..].iter().take(4).collect::<String>().to_lowercase();
                if rest.starts_with("sqrt") {
                    tokens.push(Token::Sqrt);
                    i += 4;
                    continue;
                }
                return Err(ExprError::Syntax(format!("Unexpected “{c}”")));
            }
        };
        if let Some(token) = single {
            tokens.push(token);
        }
        i += 1;
    }
    Ok(tokens)
}

struct Parser {
    tokens: Vec<Token>,
    pos: usize,
}

impl Parser {
    fn peek(&self) -> Option<&Token> {
        self.tokens.get(self.pos)
    }

    fn next(&mut self) -> Option<Token> {
        let token = self.tokens.get(self.pos).cloned();
        self.pos += 1;
        token
    }

    fn expr(&mut self) -> Result<Num, ExprError> {
        let mut value = self.term()?;
        loop {
            match self.peek() {
                Some(Token::Plus) => {
                    self.pos += 1;
                    value = value.add(&self.term()?)?;
                }
                Some(Token::Minus) => {
                    self.pos += 1;
                    value = value.sub(&self.term()?)?;
                }
                _ => return Ok(value),
            }
        }
    }

    fn term(&mut self) -> Result<Num, ExprError> {
        let mut value = self.unary()?;
        loop {
            match self.peek() {
                Some(Token::Times) => {
                    self.pos += 1;
                    value = value.mul(&self.unary()?)?;
                }
                Some(Token::Divide) => {
                    self.pos += 1;
                    value = value.div(&self.unary()?)?;
                }
                _ => return Ok(value),
            }
        }
    }

    fn unary(&mut self) -> Result<Num, ExprError> {
        match self.peek() {
            Some(Token::Minus) => {
                self.pos += 1;
                Ok(self.unary()?.negate())
            }
            Some(Token::Plus) => {
                self.pos += 1;
                self.unary()
            }
            _ => self.power(),
        }
    }

    fn power(&mut self) -> Result<Num, ExprError> {
        let base = self.postfix()?;
        if self.peek() == Some(&Token::Power) {
            self.pos += 1;
            let exponent = self.unary()?;
            let n = exponent
                .to_integer()
                .filter(|n| n.abs() <= 100_000)
                .ok_or_else(|| ExprError::Syntax("Only whole powers are supported".into()))?;
            return Ok(base.powi(n)?);
        }
        Ok(base)
    }

    fn postfix(&mut self) -> Result<Num, ExprError> {
        let mut value = self.primary()?;
        while self.peek() == Some(&Token::Percent) {
            self.pos += 1;
            value = value.div(&Num::from_i64(100))?;
        }
        Ok(value)
    }

    fn primary(&mut self) -> Result<Num, ExprError> {
        match self.next() {
            Some(Token::Num(value)) => Ok(value),
            Some(Token::Open) => {
                let value = self.expr()?;
                match self.next() {
                    Some(Token::Close) => Ok(value),
                    _ => Err(ExprError::Syntax("Missing “)”".into())),
                }
            }
            Some(Token::Sqrt) => Ok(self.primary()?.sqrt()?),
            Some(_) => Err(ExprError::Syntax("Unexpected operator".into())),
            None => Err(ExprError::Syntax("Incomplete expression".into())),
        }
    }
}

pub fn evaluate(text: &str, fmt: &NumberFormat) -> Result<Num, ExprError> {
    let tokens = tokenize(text, fmt)?;
    if tokens.is_empty() {
        return Err(ExprError::Syntax("Empty expression".into()));
    }
    let mut parser = Parser { tokens, pos: 0 };
    let value = parser.expr()?;
    if parser.pos < parser.tokens.len() {
        return Err(ExprError::Syntax("Unexpected “)” or missing operator".into()));
    }
    Ok(value)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn eval(text: &str) -> String {
        match evaluate(text, &NumberFormat::default()) {
            Ok(v) => v.canonical(),
            Err(e) => format!("error: {}", e.message()),
        }
    }

    fn eval_pl(text: &str) -> String {
        let fmt = NumberFormat { decimal: ",".into(), group: "\u{a0}".into() };
        evaluate(text, &fmt).map(|v| v.canonical()).unwrap_or_else(|e| e.message())
    }

    #[test]
    fn precedence_and_parentheses() {
        assert_eq!(eval("2+3*4"), "14");
        assert_eq!(eval("(2+3)*4"), "20");
        assert_eq!(eval("2+3*(4-1)"), "11");
        assert_eq!(eval("10/4"), "2.5");
        assert_eq!(eval("0.1+0.2"), "0.3");
        assert_eq!(eval("-(3-5)*2"), "4");
        assert_eq!(eval("2 \u{d7} 3 \u{f7} 4 \u{2212} 1"), "0.5");
    }

    #[test]
    fn powers_percent_sqrt() {
        assert_eq!(eval("2^10"), "1024");
        assert_eq!(eval("2^3^2"), "512");
        assert_eq!(eval("-2^2"), "-4");
        assert_eq!(eval("2^-1"), "0.5");
        assert_eq!(eval("50%"), "0.5");
        assert_eq!(eval("200*15%"), "30");
        assert_eq!(eval("\u{221a}16+sqrt(9)"), "7");
    }

    #[test]
    fn localized_numbers() {
        assert_eq!(eval_pl("1\u{a0}234,5*2"), "2469");
        assert_eq!(eval_pl("2,5+2,5"), "5");
        assert_eq!(eval("1,234.5*2"), "2469");
    }

    #[test]
    fn errors() {
        assert_eq!(eval("1/0"), "error: Cannot divide by zero");
        assert_eq!(eval("(1+2"), "error: Missing “)”");
        assert_eq!(eval("2+"), "error: Incomplete expression");
        assert_eq!(eval("2^0.5"), "error: Only whole powers are supported");
        assert_eq!(eval("abc"), "error: Unexpected “a”");
        assert_eq!(eval("2 3"), "error: Unexpected “)” or missing operator");
        assert_eq!(eval("\u{221a}(-4)"), "error: Invalid input");
    }
}
