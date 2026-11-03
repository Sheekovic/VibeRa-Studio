// SPDX-License-Identifier: AGPL-3.0-or-later
// Original actual-JUCE prepared-manager tests. UNBUILT/UNRUN draft.
#pragma once
#include "CallbackAllocationProbe.h"
#include <limits>
#include <stdexcept>

namespace vibera::draft::prepared_history
{
struct Counters { int performs=0, undoes=0, destroys=0; };
class FrozenAction final : public juce::UndoableAction
{
  public:
    int &value;
    const int before, after, units;
    Counters &calls;
    FrozenAction(int &target, int oldValue, int newValue, Counters &counts, int cost=1)
        : value(target), before(oldValue), after(newValue), units(cost), calls(counts) {}
    ~FrozenAction() override { ++calls.destroys; }
    bool perform() override { ++calls.performs; value=after; return true; }
    bool undo() override { ++calls.undoes; value=before; return true; }
    int getSizeInUnits() override { return units; }
};
class ForeignAction final : public juce::UndoableAction
{
    bool perform() override { return true; }
    bool undo() override { return true; }
    int getSizeInUnits() override { return 1; }
};
inline bool cachedUnits(const juce::UndoableAction &action, int &units, void *) noexcept
{
    if (const auto *known=dynamic_cast<const FrozenAction *>(&action))
    { units=known->units; return units>=0; }
    return false;
}
struct Apply
{
    int &value;
    int target=0,calls=0;
    static void swapCanonical(void *context) noexcept
    {
        auto &self=*static_cast<Apply *>(context);
        self.value=self.target;
        ++self.calls;
    }
};
inline juce::String snapshot(const juce::UndoManager &manager, int value)
{
    return juce::String(value)+"/"+juce::String(manager.getNumberOfUnitsTakenUpByStoredCommands())+
           "/"+juce::String(manager.getNumActionsInCurrentTransaction())+
           "/"+manager.getUndoDescriptions().joinIntoString("|")+
           "/"+manager.getRedoDescriptions().joinIntoString("|")+
           "/"+manager.getCurrentTransactionName();
}
inline void ordinary(juce::UndoManager &manager,int &value,int next,Counters &calls,const char *name)
{
    manager.beginNewTransaction(name);
    require(manager.perform(new FrozenAction(value,value,next,calls)),"Original ordinary JUCE action succeeds");
}
inline void noAllocationCommit(juce::UndoManager &manager,juce::UndoManager::PreparedHistoryMutation &ticket,
                               Apply &apply)
{
    const auto allocations=callbackAllocations.load(),destructions=callbackDestructions.load();
    const bool previous=callbackAllocationProbe;
    callbackAllocationProbe=true;
    const bool committed=manager.commitPreparedMutation(ticket,&Apply::swapCanonical,&apply);
    callbackAllocationProbe=previous;
    require(committed && ticket.wasCommitted() && apply.calls==1,
            "Prepared canonical apply occurs exactly once and real history commits");
    require(callbackAllocations.load()==allocations && callbackDestructions.load()==destructions,
            "Commit has no instrumented C++ allocation or deletion (not CRT allocator interception)");
}
inline void coreTests()
{
    int value=0;Counters counts;juce::UndoManager manager;
    ordinary(manager,value,1,counts,"one");ordinary(manager,value,2,counts,"two");
    const auto original=snapshot(manager,value);
    auto ticket=manager.prepareNewTransaction(std::make_unique<FrozenAction>(value,2,3,counts),
                                               "three",1,&cachedUnits,nullptr);
    require(ticket && manager.isPreparedMutationCurrent(*ticket) && snapshot(manager,value)==original,
            "New preparation does not apply action/model/history metadata");
    juce::Array<const juce::UndoableAction *> actions;
    require(manager.getPreparedMutationActions(*ticket,actions) && actions.size()==1 &&
                dynamic_cast<const FrozenAction *>(actions[0])->after==3,
            "Prepared discovery identifies actual incoming canonical action");
    const auto performs=counts.performs,undoes=counts.undoes,destroys=counts.destroys;
    Apply apply{value,3};noAllocationCommit(manager,*ticket,apply);
    require(value==3 && manager.getUndoDescription()=="three" && counts.performs==performs &&
                counts.undoes==undoes && counts.destroys==destroys,
            "Prepared history records already-applied snapshot without executing/deleting actions");
    require(!manager.commitPreparedMutation(*ticket,&Apply::swapCanonical,&apply) && apply.calls==1,
            "Consumed ticket does not repeat canonical mutation");
    auto undo=manager.prepareUndoMutation();require(bool(undo),"Real current transaction available");
    actions.clear();require(manager.getPreparedMutationActions(*undo,actions) && actions.size()==1,
                            "Undo exposes actual manager-owned transaction");
    Apply backwards{value,dynamic_cast<const FrozenAction *>(actions[0])->before};
    noAllocationCommit(manager,*undo,backwards);
    require(value==2 && manager.canRedo() && counts.undoes==undoes,
            "Aggregate Undo moves actual cursor without fallible action execution");
    auto redo=manager.prepareRedoMutation();require(bool(redo),"Real next transaction available");
    actions.clear();manager.getPreparedMutationActions(*redo,actions);
    Apply forwards{value,dynamic_cast<const FrozenAction *>(actions[0])->after};
    noAllocationCommit(manager,*redo,forwards);
    require(value==3 && !manager.canRedo() && counts.performs==performs,"Aggregate Redo retains real cursor");
    require(manager.notifyPreparedMutation(*ticket) && !manager.notifyPreparedMutation(*ticket) &&
                ticket->wasCommitted(),"Notification is separate, idempotent and cannot negate commit");
    // Two private preparations can coexist. Only actual mutation invalidates peers.
    auto first=manager.prepareNewTransaction(std::make_unique<FrozenAction>(value,3,4,counts),"four",1,&cachedUnits,nullptr);
    auto second=manager.prepareNewTransaction(std::make_unique<FrozenAction>(value,3,5,counts),"five",1,&cachedUnits,nullptr);
    require(first && second && manager.isPreparedMutationCurrent(*first),"Preparation does not stale another ticket");
    Apply four{value,4};noAllocationCommit(manager,*first,four);
    Apply five{value,5};require(!manager.commitPreparedMutation(*second,&Apply::swapCanonical,&five) &&
                                  five.calls==0 && value==4,"First commit stales competing candidate before callback");
}
inline void invalidationTests()
{
    for(int mutation=0;mutation<8;++mutation)
    {
        int value=0;Counters counts;juce::UndoManager manager;
        ordinary(manager,value,1,counts,"one");ordinary(manager,value,2,counts,"two");
        auto pending=manager.prepareUndoMutation();require(bool(pending),"Prepare invalidation candidate");
        switch(mutation)
        {
            case 0:manager.beginNewTransaction("metadata");break;
            case 1:manager.setCurrentTransactionName("rename");break;
            case 2:manager.setMaxNumberOfStoredUnits(32,2);break;
            case 3:manager.undo();break;
            case 4:manager.clearUndoHistory();break;
            case 5:ordinary(manager,value,3,counts,"three");break;
            case 6:manager.undo();manager.redo();break;
            case 7:manager.undoCurrentTransactionOnly();break;
        }
        const auto afterMutation=snapshot(manager,value);
        Apply apply{value,-100};
        require(!manager.isPreparedMutationCurrent(*pending) &&
                    !manager.commitPreparedMutation(*pending,&Apply::swapCanonical,&apply) && apply.calls==0 &&
                    snapshot(manager,value)==afterMutation,"Metadata/cursor/stash mutation stales before any canonical apply");
    }
    int value=0;Counters counts;juce::UndoManager manager,other;
    ordinary(manager,value,1,counts,"one");auto ticket=manager.prepareUndoMutation();Apply apply{value,0};
    require(!other.commitPreparedMutation(*ticket,&Apply::swapCanonical,&apply) && apply.calls==0,
            "Prepared ticket cannot cross managers");
    require(!manager.prepareNewTransaction(std::make_unique<FrozenAction>(value,1,2,counts),"bad-cost",2,&cachedUnits,nullptr),
            "Provided units are verified against admitted immutable action");
    manager.beginNewTransaction("foreign");manager.perform(new ForeignAction());
    const auto before=snapshot(manager,value);
    require(!manager.prepareNewTransaction(std::make_unique<FrozenAction>(value,1,2,counts),"closed",1,&cachedUnits,nullptr) &&
                snapshot(manager,value)==before,"Foreign existing history refuses closed prepared-new contract");
}
inline void stashAndPruneTests()
{
    int value=0;Counters counts;juce::UndoManager manager;
    ordinary(manager,value,1,counts,"one");ordinary(manager,value,2,counts,"two");manager.undo();
    auto branch=manager.prepareNewTransaction(std::make_unique<FrozenAction>(value,1,10,counts),"branch",1,&cachedUnits,nullptr);
    require(bool(branch),"Prepare actual redo-branch replacement");Apply ten{value,10};noAllocationCommit(manager,*branch,ten);
    require(manager.undoCurrentTransactionOnly() && value==1 && manager.getRedoDescription()=="two" &&
                manager.redo() && value==2,"Generic undoCurrentTransactionOnly restores exact pre-existing future owners");
    // Existing stash survives another prepared transaction when no new redo future exists.
    manager.undo();ordinary(manager,value,3,counts,"ordinary-branch");
    auto follow=manager.prepareNewTransaction(std::make_unique<FrozenAction>(value,3,4,counts),"follow",1,&cachedUnits,nullptr);
    require(bool(follow),"Prepare with existing untouched stash");Apply four{value,4};noAllocationCommit(manager,*follow,four);
    require(manager.undoCurrentTransactionOnly() && value==3 && manager.getRedoDescription()=="two" &&
                manager.redo() && value==2,"Prepared transaction preserves legacy stash restore metadata exactly");
    int prunedValue=0;Counters prunedCalls;juce::UndoManager pruning;
    ordinary(pruning,prunedValue,1,prunedCalls,"old");ordinary(pruning,prunedValue,2,prunedCalls,"retained");
    pruning.setMaxNumberOfStoredUnits(2,1);const int beforeDestroy=prunedCalls.destroys;
    auto next=pruning.prepareNewTransaction(std::make_unique<FrozenAction>(prunedValue,2,3,prunedCalls),"new",1,&cachedUnits,nullptr);
    require(bool(next),"Prepare bounded pruning");Apply three{prunedValue,3};noAllocationCommit(pruning,*next,three);
    require(prunedCalls.destroys==beforeDestroy && pruning.getUndoDescriptions().size()==2 &&
                pruning.getNumberOfUnitsTakenUpByStoredCommands()==2,"Pruned owners remain pinned through commit");
    next.reset();require(prunedCalls.destroys==beforeDestroy+1,"Only later control retirement destroys pruned action");
}
inline void cancellationAndCostTests()
{
    int value=0;Counters counts;juce::UndoManager manager;
    ordinary(manager,value,1,counts,"one");ordinary(manager,value,2,counts,"two");
    const auto before=snapshot(manager,value);const int destroys=counts.destroys;
    {
        auto abandoned=manager.prepareNewTransaction(std::make_unique<FrozenAction>(value,2,3,counts),"abandoned",1,&cachedUnits,nullptr);
        require(bool(abandoned),"Prepare cancellable private action");
    }
    require(snapshot(manager,value)==before && counts.destroys==destroys+1,
            "Abandoning borrowed prepared tables destroys only incoming private action");
    auto stale=manager.prepareNewTransaction(std::make_unique<FrozenAction>(value,2,3,counts),"stale",1,&cachedUnits,nullptr);
    require(bool(stale),"Prepare old-pointer lifetime fixture");
    manager.clearUndoHistory();
    require(counts.destroys==destroys+3,"Generic clear destroys both real old actions despite uncommitted borrowed table");
    stale.reset();require(counts.destroys==destroys+4,"Stale borrowed table never double-deletes already-cleared real owners");
    require(!manager.prepareNewTransaction(std::make_unique<ForeignAction>(),"foreign-new",1,&cachedUnits,nullptr),
            "Incoming unknown action is refused by the same cached-cost admission reader");
    require(!manager.prepareNewTransaction(std::make_unique<FrozenAction>(value,2,3,counts,-1),"negative",-1,&cachedUnits,nullptr),
            "Negative action cost refuses without arithmetic overflow");
    int hugeValue=0;Counters hugeCalls;
    juce::UndoManager huge(std::numeric_limits<int>::max(),1);
    huge.beginNewTransaction("huge");
    require(huge.perform(new FrozenAction(hugeValue,0,1,hugeCalls,std::numeric_limits<int>::max())),"Prepare exact maximum old accounting");
    const auto hugeBefore=snapshot(huge,hugeValue);
    require(!huge.prepareNewTransaction(std::make_unique<FrozenAction>(hugeValue,1,2,hugeCalls),"overflow",1,&cachedUnits,nullptr) &&
                snapshot(huge,hugeValue)==hugeBefore,"Cost sum overflow refuses without changing actual history");
}
inline void allocationFaultTests()
{
    int refusalCount=0,successCount=0;
    for(int site=0;site<64;++site)
    {
        int value=0;Counters counts;juce::UndoManager manager;
        ordinary(manager,value,1,counts,"one");ordinary(manager,value,2,counts,"two");manager.undo();
        const auto before=snapshot(manager,value);
        juce::Array<const juce::UndoableAction *> oldUndo,oldRedo;
        manager.getActionsInUndoTransaction(oldUndo);manager.getActionsInRedoTransaction(oldRedo);
        std::unique_ptr<juce::UndoManager::PreparedHistoryMutation> ticket;
        bool failed=false;
        preparationAllocationFailureHit=false;preparationAllocationFailureAfter=site;
        try { ticket=manager.prepareNewTransaction(std::make_unique<FrozenAction>(value,1,9,counts),"fault",1,&cachedUnits,nullptr); }
        catch(const std::bad_alloc &) { failed=true; }
        preparationAllocationFailureAfter=-1;
        require(snapshot(manager,value)==before,"Every preparation allocation failure preserves actual model/history/redo");
        juce::Array<const juce::UndoableAction *> nowUndo,nowRedo;
        manager.getActionsInUndoTransaction(nowUndo);manager.getActionsInRedoTransaction(nowRedo);
        require(oldUndo==nowUndo && oldRedo==nowRedo,"Every fault retains exact old Undo/Redo action owners");
        if(failed)
        {
            require(preparationAllocationFailureHit,"Fault branch reached actual injected C++ allocation");++refusalCount;
            auto recovery=manager.prepareNewTransaction(std::make_unique<FrozenAction>(value,1,9,counts),"recovery",1,&cachedUnits,nullptr);
            require(bool(recovery),"Refused preparation permits a later genuine candidate");
        }
        else
        {
            require(bool(ticket),"Non-faulted known preparation succeeds");Apply apply{value,9};noAllocationCommit(manager,*ticket,apply);++successCount;
            if(successCount==2)break;
        }
    }
    require(refusalCount>0 && successCount==2,"Bounded sweep reaches failures and two complete preparations");
}
inline void tests(const juce::File &folder)
{
    coreTests();invalidationTests();stashAndPruneTests();cancellationAndCostTests();allocationFaultTests();
    require(folder.getChildFile("prepared-history.txt").replaceWithText(
                "PASS actual JUCE manager prepared new/Undo/Redo, freshness, cached admission, redo stash, pruning retirement and C++ allocation-fault checks. "
                "No Controller Project/graph/native/audio/hardware/listening or CRT allocator qualification.\n"),
            "Retain prepared-manager test result");
}
} // namespace vibera::draft::prepared_history
