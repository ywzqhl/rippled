//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_FEATURES_H
#define RIPPLE_FEATURES_H

#include "../book/Types.h"

namespace ripple {

class FeatureSet
{
    // the status of all features requested in a given window
public:
    std::uint32_t mCloseTime;
    int mTrustedValidations;                    // number of trusted validations
    ripple::unordered_map<uint256, int> mVotes; // yes votes by feature

    FeatureSet (std::uint32_t ct) : mCloseTime (ct), mTrustedValidations (0)
    {
        ;
    }
    void addVoter ()
    {
        ++mTrustedValidations;
    }
    void addVote (uint256 const& feature)
    {
        ++mVotes[feature];
    }
};

class FeatureState
{
public:
    bool mVetoed;   // We don't want this feature enabled
    bool mEnabled;
    bool mSupported;
    bool mDefault;  // Include in genesis ledger

    core::Clock::time_point m_firstMajority; // First time we saw a majority (close time)
    core::Clock::time_point m_lastMajority;  // Most recent time we saw a majority (close time)

    std::string mFriendlyName;

    FeatureState ()
        : mVetoed (false), mEnabled (false), mSupported (false), mDefault (false),
          m_firstMajority (0), m_lastMajority (0)
    {
        ;
    }

    void setVeto ()
    {
        mVetoed = true;
    }
    void setDefault ()
    {
        mDefault = true;
    }
    bool isDefault ()
    {
        return mDefault;
    }
    bool isSupported ()
    {
        return mSupported;
    }
    bool isVetoed ()
    {
        return mVetoed;
    }
    bool isEnabled ()
    {
        return mEnabled;
    }
    const std::string& getFiendlyName ()
    {
        return mFriendlyName;
    }
    void setFriendlyName (const std::string& n)
    {
        mFriendlyName = n;
    }
};

/** Feature table interface.

    The feature table stores the list of enabled and potential features.
    Individuals features are voted on by validators during the consensus
    process.
*/
class FeatureTable
{
public:
    /** Create a new FeatureTable.

        @param majorityTime the number of seconds a feature must hold a majority
                            before we're willing to vote yes on it.
        @param majorityFraction ratio, out of 256, of servers that must say
                                they want a feature before we consider it to
                                have a majority.
        @param journal
    */

    virtual ~FeatureTable() { }

    virtual void addInitial () = 0;

    virtual FeatureState* addKnown (const char* featureID,
        const char* friendlyName, bool veto) = 0;
    virtual uint256 get (const std::string& name) = 0;

    virtual bool veto (uint256 const& feature) = 0;
    virtual bool unVeto (uint256 const& feature) = 0;

    virtual bool enable (uint256 const& feature) = 0;
    virtual bool disable (uint256 const& feature) = 0;

    virtual bool isEnabled (uint256 const& feature) = 0;
    virtual bool isSupported (uint256 const& feature) = 0;

    virtual void setEnabled (const std::vector<uint256>& features) = 0;
    virtual void setSupported (const std::vector<uint256>& features) = 0;

    virtual void reportValidations (const FeatureSet&) = 0;

    virtual Json::Value getJson (int) = 0;
    virtual Json::Value getJson (uint256 const& ) = 0;

    virtual void
    doValidation (Ledger::ref lastClosedLedger, STObject& baseValidation) = 0;
    virtual void
    doVoting (Ledger::ref lastClosedLedger, SHAMap::ref initialPosition) = 0;
};

std::unique_ptr<FeatureTable>
make_FeatureTable (std::chrono::seconds majorityTime, int majorityFraction,
    beast::Journal journal);

} // ripple

#endif
