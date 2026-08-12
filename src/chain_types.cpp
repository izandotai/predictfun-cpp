#include "predictfun/types/chain.hpp"

namespace predictfun {

RpcEndpoint default_rpc_endpoint(ChainId chain_id) {
  if (chain_id == ChainId::bnb_mainnet)
    return RpcEndpoint{"bsc-dataseed.bnbchain.org", "443", "/", true};
  return RpcEndpoint{"bsc-testnet-dataseed.bnbchain.org", "443", "/", true};
}

} // namespace predictfun
