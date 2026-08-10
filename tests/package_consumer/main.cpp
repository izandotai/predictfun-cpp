#include "predictfun/types/decimal.hpp"

int main() {
  const auto price = predictfun::Price::parse("0.42", 2);
  return price && price.value().ticks() == 42U ? 0 : 1;
}
