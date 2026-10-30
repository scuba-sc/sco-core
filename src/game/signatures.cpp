#include "sco/game/signatures.h"
#include "sco/signatures.h"
#include <cstddef>
#include <iterator>

namespace sco::game {

extern const SigDef kTeleportSignatures[];
extern const size_t kTeleportSignatureCount;
extern const SigDef kSystemSignatures[];
extern const size_t kSystemSignatureCount;
extern const SigDef kPakSignatures[];
extern const size_t kPakSignatureCount;
extern const SigDef kDataCoreLiveSignatures[];
extern const size_t kDataCoreLiveSignatureCount;
extern const SigDef kAsopSignatures[];
extern const size_t kAsopSignatureCount;
extern const SigDef kAtcSignatures[];
extern const size_t kAtcSignatureCount;
extern const SigDef kHangarSignatures[];
extern const size_t kHangarSignatureCount;
extern const SigDef kFeatureSignatures[];
extern const size_t kFeatureSignatureCount;
extern const SigDef kOfflineSignatures[];
extern const size_t kOfflineSignatureCount;
extern const SigDef kContractsSignatures[];
extern const size_t kContractsSignatureCount;
extern const SigDef kActorsSignatures[];
extern const size_t kActorsSignatureCount;
extern const SigDef kWorldSignatures[];
extern const size_t kWorldSignatureCount;
extern const SigDef kMiningSignatures[];
extern const size_t kMiningSignatureCount;
extern const SigDef kGameRulesSignatures[];
extern const size_t kGameRulesSignatureCount;
extern const SigDef kEntitiesSignatures[];
extern const size_t kEntitiesSignatureCount;
extern const SigDef kEventsSignatures[];
extern const size_t kEventsSignatureCount;
extern const SigDef kActorStatsSignatures[];
extern const size_t kActorStatsSignatureCount;
extern const SigDef kInventorySignatures[];
extern const size_t kInventorySignatureCount;

SigRegistrationResult RegisterGameSignatures(SignatureRegistry& registry) {
    const SignatureTable kGameSignatureTables[] = {
        {kTeleportSignatures, kTeleportSignatureCount},
        {kSystemSignatures, kSystemSignatureCount},
        {kPakSignatures, kPakSignatureCount},
        {kDataCoreLiveSignatures, kDataCoreLiveSignatureCount},
        {kAsopSignatures, kAsopSignatureCount},
        {kAtcSignatures, kAtcSignatureCount},
        {kHangarSignatures, kHangarSignatureCount},
        {kFeatureSignatures, kFeatureSignatureCount},
        {kOfflineSignatures, kOfflineSignatureCount},
        {kContractsSignatures, kContractsSignatureCount},
        {kActorsSignatures, kActorsSignatureCount},
        {kWorldSignatures, kWorldSignatureCount},
        {kMiningSignatures, kMiningSignatureCount},
        {kEntitiesSignatures, kEntitiesSignatureCount},
        {kEventsSignatures, kEventsSignatureCount},
        {kActorStatsSignatures, kActorStatsSignatureCount},
        {kInventorySignatures, kInventorySignatureCount},
        {kGameRulesSignatures, kGameRulesSignatureCount},
    };
    return registry.RegisterTables(kGameSignatureTables, std::size(kGameSignatureTables));
}

}  // namespace sco::game
