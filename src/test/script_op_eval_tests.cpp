#include <boost/assert.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/assign/list_inserter.hpp>
#include <boost/assign/std/vector.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/foreach.hpp>

#include "../main.h"
#include "../script.h"
#include "../wallet.h"

using namespace std;

// Test routines internal to script.cpp:
extern uint256 SignatureHash(CScript scriptCode, const CTransaction& txTo, unsigned int nIn, int nHashType);
extern bool VerifyScript(const CScript& scriptSig, const CScript& scriptPubKey, const CTransaction& txTo, unsigned int nIn, int& nSigOps,
                         int nHashType, bool fStrictOpEval);

BOOST_AUTO_TEST_SUITE(script_op_eval_tests)

BOOST_AUTO_TEST_CASE(script_op_eval1)
{
    // OP_EVAL looks like this:
    // scriptSig:    <sig> <sig...> <serialized_script>
    // scriptPubKey: DUP HASH160 <hash> EQUALVERIFY EVAL

    // Test SignSignature() (and therefore the version of Solver() that signs transactions)
    CBasicKeyStore keystore;
    CKey key[4];
    for (int i = 0; i < 4; i++)
    {
        key[i].MakeNewKey();
        keystore.AddKey(key[i]);
    }

    // 8 Scripts: checking all combinations of
    // different keys, straight/EVAL, pubkey/pubkeyhash
    CScript standardScripts[4];
    standardScripts[0] << key[0].GetPubKey() << OP_CHECKSIG;
    standardScripts[1].SetBitcoinAddress(key[1].GetPubKey());
    standardScripts[2] << key[1].GetPubKey() << OP_CHECKSIG;
    standardScripts[3].SetBitcoinAddress(key[2].GetPubKey());
    CScript evalScripts[4];
    uint160 sigScriptHashes[4];
    for (int i = 0; i < 4; i++)
    {
        sigScriptHashes[i] = Hash160(standardScripts[i]);
        keystore.AddCScript(sigScriptHashes[i], standardScripts[i]);
        evalScripts[i] << OP_DUP << OP_HASH160 << sigScriptHashes[i] << OP_EQUALVERIFY << OP_EVAL;
    }

    CTransaction txFrom;  // Funding transaction:
    txFrom.vout.resize(8);
    for (int i = 0; i < 4; i++)
    {
        txFrom.vout[i].scriptPubKey = evalScripts[i];
        txFrom.vout[i+4].scriptPubKey = standardScripts[i];
    }
    BOOST_CHECK(txFrom.IsStandard());

    CTransaction txTo[8]; // Spending transactions
    for (int i = 0; i < 8; i++)
    {
        txTo[i].vin.resize(1);
        txTo[i].vout.resize(1);
        txTo[i].vin[0].prevout.n = i;
        txTo[i].vin[0].prevout.hash = txFrom.GetHash();
        txTo[i].vout[0].nValue = 1;
        BOOST_CHECK_MESSAGE(IsMine(keystore, txFrom.vout[i].scriptPubKey), strprintf("IsMine %d", i));
    }
    for (int i = 0; i < 8; i++)
    {
        BOOST_CHECK_MESSAGE(SignSignature(keystore, txFrom, txTo[i], 0), strprintf("SignSignature %d", i));
    }
    // All of the above should be OK, and the txTos have valid signatures
    // Check to make sure signature verification fails if we use the wrong ScriptSig:
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 8; j++)
        {
            CScript sigSave = txTo[i].vin[0].scriptSig;
            txTo[i].vin[0].scriptSig = txTo[j].vin[0].scriptSig;
            int nUnused = 0;
            bool sigOK = VerifySignature(txFrom, txTo[i], 0, nUnused);
            if (i == j)
                BOOST_CHECK_MESSAGE(sigOK, strprintf("VerifySignature %d %d", i, j));
            else
                BOOST_CHECK_MESSAGE(!sigOK, strprintf("VerifySignature %d %d", i, j));
            txTo[i].vin[0].scriptSig = sigSave;
        }
}

BOOST_AUTO_TEST_CASE(script_op_eval2)
{
    // Test OP_EVAL edge cases

    // Make sure infinite recursion fails to validate:
    CScript infiniteRecurse;
    infiniteRecurse << OP_DUP << OP_EVAL;

    uint160 infiniteRecurseHash = Hash160(infiniteRecurse);

    CScript fund1;
    fund1 << OP_DUP << OP_HASH160 << infiniteRecurseHash << OP_EQUALVERIFY << OP_EVAL;

    CTransaction txFrom1;  // Funding transaction:
    txFrom1.vout.resize(1);
    txFrom1.vout[0].scriptPubKey = fund1;

    BOOST_CHECK(txFrom1.IsStandard()); // Looks like a standard transaction until you try to spend it

    std::vector<unsigned char> infiniteRecurseSerialized(infiniteRecurse);

    CTransaction txTo1;
    txTo1.vin.resize(1);
    txTo1.vout.resize(1);
    txTo1.vin[0].prevout.n = 0;
    txTo1.vin[0].prevout.hash = txFrom1.GetHash();
    txTo1.vin[0].scriptSig = CScript() << infiniteRecurseSerialized << infiniteRecurseSerialized;
    txTo1.vout[0].nValue = 1;

    int nUnused1 = 0;
    BOOST_CHECK(!VerifyScript(txTo1.vin[0].scriptSig, txFrom1.vout[0].scriptPubKey, txTo1, 0, nUnused1, 0, true));
    BOOST_CHECK(!VerifySignature(txFrom1, txTo1, 0, nUnused1, true));

    // Make sure 3-level-deep recursion fails to validate:
    CScript recurse3;
    recurse3 << OP_EVAL;

    uint160 recurse3Hash = Hash160(recurse3);

    CScript fund2;
    fund2 << OP_DUP << OP_HASH160 << recurse3Hash << OP_EQUALVERIFY << OP_EVAL;

    CTransaction txFrom2;  // Funding transaction:
    txFrom2.vout.resize(1);
    txFrom2.vout[0].scriptPubKey = fund2;

    BOOST_CHECK(txFrom2.IsStandard()); // Looks like a standard transaction until you try to spend it

    std::vector<unsigned char> recurse3Serialized(recurse3);
    CScript op1Script = CScript() << OP_1;
    std::vector<unsigned char> op1Serialized(op1Script);

    CTransaction txTo2;
    txTo2.vin.resize(1);
    txTo2.vout.resize(1);
    txTo2.vin[0].prevout.n = 0;
    txTo2.vin[0].prevout.hash = txFrom2.GetHash();
    txTo2.vin[0].scriptSig = CScript() << op1Serialized << recurse3Serialized << recurse3Serialized;
    txTo2.vout[0].nValue = 1;

    int nUnused2 = 0;
    BOOST_CHECK(!VerifyScript(txTo2.vin[0].scriptSig, txFrom2.vout[0].scriptPubKey, txTo2, 0, nUnused2, 0, true));
    BOOST_CHECK(!VerifySignature(txFrom2, txTo2, 0, nUnused2, true));
}

BOOST_AUTO_TEST_CASE(script_op_eval3)
{
    // Test the CScript::Set* methods
    CBasicKeyStore keystore;
    CKey key[4];
    std::vector<CKey> keys;
    for (int i = 0; i < 4; i++)
    {
        key[i].MakeNewKey();
        keystore.AddKey(key[i]);
        keys.push_back(key[i]);
    }

    CScript inner[4];
    inner[0].SetBitcoinAddress(key[0].GetPubKey());
    inner[1].SetMultisig(2, std::vector<CKey>(keys.begin(), keys.begin()+2));
    inner[2].SetMultisig(1, std::vector<CKey>(keys.begin(), keys.begin()+2));
    inner[3].SetMultisig(2, std::vector<CKey>(keys.begin(), keys.begin()+3));

    CScript outer[4];
    for (int i = 0; i < 4; i++)
    {
        outer[i].SetEval(inner[i]);
        keystore.AddCScript(Hash160(inner[i]), inner[i]);
    }

    CTransaction txFrom;  // Funding transaction:
    txFrom.vout.resize(4);
    for (int i = 0; i < 4; i++)
    {
        txFrom.vout[i].scriptPubKey = outer[i];
    }
    BOOST_CHECK(txFrom.IsStandard());

    CTransaction txTo[4]; // Spending transactions
    for (int i = 0; i < 4; i++)
    {
        txTo[i].vin.resize(1);
        txTo[i].vout.resize(1);
        txTo[i].vin[0].prevout.n = i;
        txTo[i].vin[0].prevout.hash = txFrom.GetHash();
        txTo[i].vout[0].nValue = 1;
        txTo[i].vout[0].scriptPubKey = inner[i];
        BOOST_CHECK_MESSAGE(IsMine(keystore, txFrom.vout[i].scriptPubKey), strprintf("IsMine %d", i));
    }
    for (int i = 0; i < 4; i++)
    {
        BOOST_CHECK_MESSAGE(SignSignature(keystore, txFrom, txTo[i], 0), strprintf("SignSignature %d", i));
        BOOST_CHECK_MESSAGE(txTo[i].IsStandard(), strprintf("txTo[%d].IsStandard", i));
    }
}

BOOST_AUTO_TEST_CASE(script_op_eval_backcompat1)
{
    // Check backwards-incompatibility-testing code
    CScript returnsEleven;
    returnsEleven << OP_11;

    // This should validate on new clients, but would
    // be invalid on old clients (that interpret OP_EVAL as a no-op)
    //  ... except there aren't any old clients
    CScript fund;
    fund << OP_EVAL << OP_11 << OP_EQUAL;

    CTransaction txFrom;  // Funding transaction:
    txFrom.vout.resize(1);
    txFrom.vout[0].scriptPubKey = fund;

    CTransaction txTo;
    txTo.vin.resize(1);
    txTo.vout.resize(1);
    txTo.vin[0].prevout.n = 0;
    txTo.vin[0].prevout.hash = txFrom.GetHash();
    txTo.vin[0].scriptSig = CScript() << static_cast<std::vector<unsigned char> >(returnsEleven);
    txTo.vout[0].nValue = 1;

    int nUnused = 0;
    BOOST_CHECK(VerifyScript(txTo.vin[0].scriptSig, txFrom.vout[0].scriptPubKey, txTo, 0, nUnused, 0, true));
    BOOST_CHECK(VerifySignature(txFrom, txTo, 0, nUnused, true));
}

BOOST_AUTO_TEST_CASE(script_op_eval_switchover)
{
    // Test OP_EVAL switchover code
    CScript notValid;
    notValid << OP_11 << OP_12 << OP_EQUALVERIFY;

    // This will be valid under old rules, invalid under new:
    CScript fund;
    fund << OP_EVAL;

    CTransaction txFrom;  // Funding transaction:
    txFrom.vout.resize(1);
    txFrom.vout[0].scriptPubKey = fund;

    CTransaction txTo;
    txTo.vin.resize(1);
    txTo.vout.resize(1);
    txTo.vin[0].prevout.n = 0;
    txTo.vin[0].prevout.hash = txFrom.GetHash();
    txTo.vin[0].scriptSig = CScript() << static_cast<std::vector<unsigned char> >(notValid);
    txTo.vout[0].nValue = 1;

    int nUnused = 0;
    BOOST_CHECK(VerifyScript(txTo.vin[0].scriptSig, txFrom.vout[0].scriptPubKey, txTo, 0, nUnused, 0, false));

    // Under strict op_eval switchover, it should be considered invalid:
    BOOST_CHECK(!VerifyScript(txTo.vin[0].scriptSig, txFrom.vout[0].scriptPubKey, txTo, 0, nUnused, 0, true));
}

BOOST_AUTO_TEST_SUITE_END()
