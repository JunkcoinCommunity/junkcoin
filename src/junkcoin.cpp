// Copyright (c) 2015-2021 The Dogecoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/random/uniform_int.hpp>
#include <boost/random/mersenne_twister.hpp>

#include "policy/policy.h"
#include "arith_uint256.h"
#include "junkcoin.h"
#include "txmempool.h"
#include "util.h"
#include "validation.h"
#include "junkcoin-fees.h"

int static generateMTRandom(unsigned int s, int range)
{
    boost::mt19937 gen(s);
    boost::uniform_int<> dist(1, range);
    return dist(gen);
}

// JunkCoin: Normally minimum difficulty blocks can only occur in between
// retarget blocks. However, once we introduce Digishield every block is
// a retarget, so we need to handle minimum difficulty on all blocks.
bool AllowDigishieldMinDifficultyForBlock(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    // check if the chain allows minimum difficulty blocks
    if (!params.fPowAllowMinDifficultyBlocks)
        return false;

    // check if the chain allows minimum difficulty blocks on recalc blocks 157500
    if ((unsigned)pindexLast->nHeight < params.nHeightEffective) { //minDifficultyConsensus.nHeightEffective
        //if (!params.fPowAllowDigishieldMinDifficultyBlocks)
        return false;
    }

    // Allow for a minimum block time if the elapsed time > 2*nTargetSpacing
    return (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing*2);
}

unsigned int CalculateDogecoinNextWorkRequired(bool fNewDifficultyProtocol, const int64_t nTargetTimespanCurrent, const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    int nHeight = pindexLast->nHeight + 1;

    int64_t nActualTimespanMin = fNewDifficultyProtocol ? (nTargetTimespanCurrent - nTargetTimespanCurrent/4) : (nTargetTimespanCurrent/4);
    int64_t nActualTimespanMax = fNewDifficultyProtocol ? (nTargetTimespanCurrent + nTargetTimespanCurrent/4) : (nTargetTimespanCurrent*4);

    int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;

    if(nHeight > 10000)
    {
        if (nActualTimespan < nActualTimespanMin)
            nActualTimespan = nActualTimespanMin;
        if (nActualTimespan > nActualTimespanMax)
            nActualTimespan = nActualTimespanMax;
    }
    else if(nHeight > 5000)
    {
        if (nActualTimespan < nActualTimespanMin/2)
            nActualTimespan = nActualTimespanMin/2;
        if (nActualTimespan > nActualTimespanMax)
            nActualTimespan = nActualTimespanMax;
    }
    else
    {
        if (nActualTimespan < nActualTimespanMin/4)
            nActualTimespan = nActualTimespanMin/4;
        if (nActualTimespan > nActualTimespanMax)
            nActualTimespan = nActualTimespanMax;
    }

    //const int64_t retargetTimespan = params.nPowTargetTimespan;
    /*

    int64_t nModulatedTimespan = nActualTimespan;
    int64_t nMaxTimespan;
    int64_t nMinTimespan;

    if (params.fDigishieldDifficultyCalculation && params.nHeightEffective <= nHeight) //DigiShield implementation - thanks to RealSolid & WDC for this code
    {
        // amplitude filter - thanks to daft27 for this code
        nModulatedTimespan = retargetTimespan + (nModulatedTimespan - retargetTimespan) / 8;

        nMinTimespan = retargetTimespan - (retargetTimespan / 4);
        nMaxTimespan = retargetTimespan + (retargetTimespan / 2);
    } else if (nHeight > 10000) {
        nMinTimespan = retargetTimespan / 4;
        nMaxTimespan = retargetTimespan * 4;
    } else if (nHeight > 5000) {
        nMinTimespan = retargetTimespan / 8;
        nMaxTimespan = retargetTimespan * 4;
    } else {
        nMinTimespan = retargetTimespan / 16;
        nMaxTimespan = retargetTimespan * 4;
    }

    // Limit adjustment step
    if (nModulatedTimespan < nMinTimespan)
        nModulatedTimespan = nMinTimespan;
    else if (nModulatedTimespan > nMaxTimespan)
        nModulatedTimespan = nMaxTimespan;*/

    // Retarget
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    arith_uint256 bnNew;
    //arith_uint256 bnOld;
    bnNew.SetCompact(pindexLast->nBits);
    bnNew *= nActualTimespan;
    bnNew /= nTargetTimespanCurrent;

    //bnOld = bnNew;
    //bnNew *= nModulatedTimespan;
    //bnNew /= retargetTimespan;

    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    return bnNew.GetCompact();
}

bool CheckAuxPowProofOfWork(const CBlockHeader& block, const Consensus::Params& params)
{
    /* Except for legacy blocks with full version 1, ensure that
       the chain ID is correct.  Legacy blocks are not allowed since
       the merge-mining start, which is checked in AcceptBlockHeader
       where the height is known.  */
    if (!block.IsLegacy() && params.fStrictChainId && block.GetChainId() != params.nAuxpowChainId)
        return error("%s : block does not have our chain ID"
                     " (got %d, expected %d, full nVersion %d)",
                     __func__, block.GetChainId(),
                     params.nAuxpowChainId, block.nVersion);

       

    /* If there is no auxpow, just check the block hash.  */
    if (!block.auxpow) {
        if (block.IsAuxpow()) {
            return error("%s : no auxpow on block with auxpow version",
                         __func__);
        }

        // We have to patch this because JunkCoin genesis block is invalid.
        if(block.GetHash() == params.hashGenesisBlock && block.IsLegacy())
        {
            return true;
        }

        if (!CheckProofOfWork(block.GetPoWHash(), block.nBits, params)) {
            return error("%s : non-AUX proof of work failed", __func__);
        }

        return true;
    }

    /* We have auxpow.  Check it.  */

    if (!block.IsAuxpow())
        return error("%s : auxpow on block with non-auxpow version", __func__);

    if (!block.auxpow->check(block.GetHash(), block.GetChainId(), params))
        return error("%s : AUX POW is not valid", __func__);
    if (!CheckProofOfWork(block.auxpow->getParentBlockPoWHash(), block.nBits, params))
        return error("%s : AUX proof of work failed", __func__);

    return true;
}

CAmount GetDogecoinBlockSubsidy(int nHeight, int nFees, const Consensus::Params& consensusParams, uint256 prevHash)
{
    CAmount nSubsidy = 50 * COIN;

    if(nHeight > 6220800)   // after about 12 years or 6220800 blocks, no more mining
        return 0;

    if(nHeight < 101)   // the first 100 blocks = 1 millon junkcoins will serve as bounty (pool, exchange, faucet, wiki etc), reserved.
    {
        nSubsidy = 1000 * COIN;
    }
    else if(nHeight < 1541)   // 1st day
    {
        nSubsidy = 500 * COIN;
    }
    else if(nHeight < 2981)   // 2nd day
    {
        nSubsidy = 200 * COIN;
    }
    else if(nHeight < 5861)   // 3rd and 4th days
    {
        nSubsidy = 100 * COIN;
    }
    else
    {
        // Subsidy is cut in half every 1036800 blocks, which will occur approximately every 2 years
        nSubsidy >>= (nHeight / 1036800); // Junkcoin: 1036.8K blocks in ~2 years

		int rand = generateMTRandom(nHeight, 100000);
		// printf("===>> nHeight = %d, Rand = %d\n", nHeight, rand);

		if(rand > 99990)
		{
			nSubsidy = 1000 * COIN;
		}
		else if (rand < 1001)
		{
			nSubsidy *= 3;
		}
    }

    return nSubsidy + nFees;
}

