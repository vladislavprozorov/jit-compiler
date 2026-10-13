fn is_prime(n: int) -> bool {
  if n < 2 {
    return false;
  }
  var d = 2;
  while d * d <= n {
    if n % d == 0 {
      return false;
    }
    d += 1;
  }
  return true;
}

fn main() -> int {
  var count = 0;
  var n = 0;
  while n < 1_000_000 {
    if is_prime(n) {
      count += 1;
    }
    n += 1;
  }
  return count;
}
