//
//  Database.cpp
//  Database1
//
//  Created by rick gessner on 4/12/19.
//  Copyright © 2019 rick gessner. All rights reserved.
//

#include "Database.hpp"
#include "View.hpp"
#include "BlockVisitor.hpp"
#include "Entity.hpp"
#include "Index.hpp"
#include "Filters.hpp"
#include "Row.hpp"

namespace ECE141 {

  bool describeBlock(const Block &aBlock, uint32_t aBlockNum) {
    
    return true; 
  }

  /*class UpdateRowBlockVisitor: public BlockVisitor {

  public:
    UpdateRowBlockVisitor(const Entity &anEntity, const Filters &aFilters, RowCollection &aRowCollection, const StringList* aFields)
    : BlockVisitor(anEntity, aFilters), rowCollection(aRowCollection), fields(aFields) {}

    StatusResult operator()(Storage &aStorage, const Block &aBlock, uint32_t aBlockNum) {
      //We probably should call the updateRow function in her
      //updateRow()
      return StatusResult{noError};
    }

    const RowCollection& getCollection() {
        return rowCollection;
    }

  protected:
    RowCollection&            rowCollection;
    const StringList*         fields;
  };*/

  class SelectRowBlockVisitor: public BlockVisitor {

  public:
    SelectRowBlockVisitor(const Entity &anEntity, const Filters &aFilters, RowCollection &aRowCollection, const StringList* aFields)
    : BlockVisitor(anEntity, aFilters), rowCollection(aRowCollection), fields(aFields) {}

    StatusResult operator()(Storage &aStorage, const Block &aBlock, uint32_t aBlockNum) {
      if('D'==aBlock.header.type) {
        uint32_t hash = const_cast<Entity&>(entity).getHash();
        if(hash==aBlock.header.extra) {
          Row *theRow = new Row(aBlock); 
          theRow->setBlockNum(aBlockNum); //Hang on to the block number for each block!
          //KeyValues &aKeyValueList = const_cast<Row&>(theRow).getColumns(); //need const_cast?
          KeyValues &aKeyValueList = theRow->getColumns();
          //std::cout << "Before matches" << std::endl;
          if (filters.matches(aKeyValueList)) {
            if(nullptr == fields){ //fields.empty()
              //std::cout << "fields.empty()" << fields.empty() << std::endl;
              rowCollection.add(theRow);  //block_num here?
            }
            else{
              KeyValues aKVList;
              for (auto field : *fields) {
                if (aKeyValueList.count(field)) {
                  aKVList.insert({field, aKeyValueList[field]});
                }
                else {
                  try {
                    Attribute& anAttribute = const_cast<Entity&>(entity).getAttribute(field);
                    Value aValue = const_cast<Entity&>(entity).getDefaultValue(anAttribute);
                    aKVList.insert({field, aValue});
                  }
                  catch(...) {
                    return StatusResult{invalidAttribute};
                    delete theRow;
                  }
                  //
                }
              }
              theRow = new Row(aKVList);
              rowCollection.add(theRow); 
            }
          }
          else {delete theRow;} 
        }
      }
      return StatusResult{noError};
    }


    const RowCollection& getCollection() {
        return rowCollection;
    }

    protected:
    //const Entity            &entity; Don't need these guys
    //const Filters           &filters; Dammit
    RowCollection&            rowCollection;
    const StringList*         fields;

  };

  
  //==================================================================
  
  // USE: This view is used to do a debug dump of db/storage container...
  class DescribeDatabaseView : public View {
  public:
    DescribeDatabaseView(Storage &aStorage) : storage{aStorage}, stream(nullptr) {}
    
    StatusResult operator()(Storage &storage, const Block &aBlock, uint32_t aBlockNum) {
      (*stream) << aBlockNum << ". block " << aBlock.header.type << "\n";
      return StatusResult{noError};
      //return true;
    }
    
    bool show(std::ostream &anOutput) {
      stream = &anOutput;
      storage.eachBlock(*this);
      return true;
    }
    
  protected:
    Storage       &storage;
    std::ostream  *stream;
  };
  
  //==================================================================
  
  // USE: This view is used to show list of entities in TOC....
  class ShowTablesView : public View {
  public:
    ShowTablesView(Block &aTOC) : toc{aTOC} {}
    
    bool show(std::ostream &anOutput) {
      for(int i=0;i<toc.entities.header.count;i++) {
        anOutput << toc.entities.items[i].name << std::endl;
      }
      return true;
    }
    
  protected:
    Block         &toc;
  };
  
  //==================================================================
  
  Database::Database(const std::string aName, CreateNewStorage)
  : name(aName), storage(aName, CreateNewStorage{}) {
    //we've created storage, but haven't loaded any entities yet...
  }
  
  Database::Database(const std::string aName, OpenExistingStorage)
  : name(aName), storage(aName, OpenExistingStorage{}) {
    //we've opened storage, but haven't loaded any entities yet...
  }
  
  Database::~Database() {
    saveEntities();
  }
  
  // USE: a child object needs a named entity for processing...
  Entity* Database::getEntity(const std::string &aName) {
    
    //STUDENT: implement this method to retrieve entity from storage...
    if(entities.count(aName)) {
      return entities[aName];
    }
    else {
      //check unloaded schema from storage...
      PersistEntity *thePE = storage.findEntityInTOC(aName);
      if(thePE && thePE->blocknum>0) {
        Block theBlock;
        StatusResult theResult=storage.readBlock(thePE->blocknum, theBlock);
        if(theResult) {
          Entity *theEntity=new Entity(theBlock,thePE->blocknum);
          entities[aName]=theEntity;
          theResult = theEntity->loadIndices(storage); //CHANGED
          return theEntity;
        }
      }
    }
    return nullptr;
  }
  
  // USE:   Add a new table (entity);
  // NOTE:  The DB assumes ownership of the entity object...
  StatusResult Database::createTable(Entity *anEntity) {
    std::string &theName=anEntity->getName();
    entities[theName]=anEntity; //Entity name added to TOC
    
    Block theBlock;
    theBlock=(*anEntity); //convert from entity...
    StatusResult theResult= storage.addEntity(theName, theBlock);
    Entity *theEntity=entities[theName];
    theEntity->blockNum=theResult.value; //hang on to the blocknum...
    
    return theResult;
  }
  
  // USE: called to actually delete rows from storage for table (via entity) with matching filters...
  StatusResult Database::deleteRows(const std::string &aName, const Filters &aFilters) {
    StatusResult result;
    if(Entity *theEntity = getEntity(aName)){
      RowCollection theCollection;
      if(result=selectRows(theCollection, *theEntity, aFilters)) {
        RowList& theRows=theCollection.getRows();
        for(auto *theRow : theRows) {
          //need to delete indices before deleting row...
          KeyValues& aList = theRow->getColumns();
          std::string primaryKey = theEntity->getPrimaryKey();
          if (!primaryKey.empty()) {
            Index* theIndex = theEntity->getIndex(primaryKey);
            if (theIndex) {
              theIndex->removeKeyValue(aList[primaryKey]);
            }
          }
          if(!(result=storage.releaseBlock(theRow->getBlockNumber()))) { 
            return result;
          }
          //else { return result; } //do we even need the else statement...
        }
        return result;
      }
      theEntity->writeIndices(storage);
      delete theEntity;
    }
    return StatusResult{unknownTable};
  }
  
  // USE: Call this to dump the db for debug purposes...
  StatusResult Database::describe(std::ostream &anOutput) {
    if(View *theView = new DescribeDatabaseView(storage)) {
      theView->show(anOutput);  //STUDENT you need to implement the view...
      delete theView;
    }
    return StatusResult{noError};
  }
  
  // USE: call this to actually remove a table from the DB...
  StatusResult Database::dropTable(const std::string &aName) {

    //STUDENT: Implement this method:
    //         1. find the Entity in storage TOC
    //         2  if found, ask storage to drop it
    //         3. if you cache entities on DB, remove it there too...
    
    PersistEntity* dump = storage.findEntityInTOC(aName);
    if (dump){
      deleteRows(aName, Filters());
      storage.dropEntity(aName);

      //TODO
      //We should now erease each block belonging to the this table.
      if (entities.find(aName) != entities.end()) {
        //need to remove indices as well...
        entities[aName]->dropIndices(storage);
        delete entities[aName];
        entities.erase(aName); //removing this entity from the cache
      }
      return StatusResult{noError};
    }
    return StatusResult{unknownTable};
  }
  
  // USE: call this to add a row into the given database...
  StatusResult Database::insertRow(const Row &aRow, const std::string &aTableName) {
    StatusResult theResult{unknownTable};
    uint32_t autoInc;
    if(Entity *theEntity=getEntity(aTableName)) {
      KeyValues &aList = const_cast<Row&>(aRow).getColumns();
      //make sure that attribute is in entity, and that data types match
      if (theEntity->validate(aList)) {
        // Now we are sure this is valid row
        // We will insert this row so incrementing theEntity's internal autoincrement variable
        autoInc = theEntity->getNextAutoIncrementValue();
        // Checking if theEntity has a primaryKey. 
        // If it has one, let's assume it is auto incremented (for now...)
        std::string attrName = theEntity->getPrimaryKey();
        if("" != attrName){
          // Finding this attribute in the aList and updating its value to the current autoincrement value
          auto it = aList.find(attrName); 
          if (it != aList.end()) {
              it->second = (int)autoInc;
          }
          else { //user likely not going to enter id when inserting row, as we need to make it for them... therefore it's not going to be in aList
            aList.insert({attrName, (int)autoInc});
          }
          // Else, we don't have to do anything (considering the primary key & auto increment assumption made above)
        }
        
        Block theBlock(aList);
        //use extra value of block header to store hash of entity
        theBlock.header.extra = theEntity->getHash();
        theBlock.header.id = autoInc;//returns a uint_32...
        theResult = storage.findFreeBlockNum();
        uint32_t blocknum = theResult.value;
        theResult = storage.writeBlock(blocknum, theBlock);

        //next we want to store primary key to index
        if (!attrName.empty()) {
          Index* theIndex = theEntity->getIndex(attrName);
          if (theIndex) {
            Value theValue = (int)autoInc;
            theIndex->addKeyValue(theValue, blocknum);
          }
        }
      }
    }
    return theResult;
  }
  

  using StorageCallback = std::function<StatusResult(Storage &aStorage, const Block &aBlock, uint32_t aBlockNum)>;


  // USE: select a set of rows that match given filters (or all rows if filters are empty)...
  StatusResult Database::selectRows(RowCollection &aCollection, const Entity &anEntity,
                                    const Filters &aFilters, const StringList *aFields) {
    //STUDENT: Get blocks for this entity from storage, decode into rows, and add to collection
    //         NOTE:  aFields (if not empty) tells you which fields to load per row;
    //                otherwise load all fields (*)
    SelectRowBlockVisitor blockV(anEntity, aFilters, aCollection, aFields);
    Index* theIndex = const_cast<Entity&>(anEntity).getIndex(const_cast<Entity&>(anEntity).getPrimaryKey());
    StatusResult theResult = theIndex ? theIndex->each(storage, blockV) : storage.eachBlock(blockV);
    //StatusResult theResult = storage.eachBlock(blockV);
    aCollection = blockV.getCollection();

    return theResult;
  }
  
  //USE: resave entities that were in memory and changed...
  StatusResult Database::saveEntities() {
    StatusResult theResult{noError};
    for (auto thePair : entities) {
      theResult = thePair.second->writeIndices(storage); //write all indices to block
      if(thePair.second->isDirty()) {
        Block theBlock=(*thePair.second);
        theResult=storage.writeBlock(thePair.second->blockNum, theBlock);
        delete thePair.second;
      }
    }
    return theResult;
  }
  
  //USE: show the list of tables in this db...
  StatusResult Database::showTables(std::ostream &anOutput) {
    
    //STUDENT: create a ShowTablesView object, and call the show() method...
    //         That view is declared at the top of this file.

    if(View *theView = new ShowTablesView(storage.toc)) {
      theView->show(anOutput);  //STUDENT you need to implement the view...
      delete theView;
    }
    
    return StatusResult{noError};
  }

  StatusResult Database::updateRow(const Row &aRow, const KeyValues &aKVList, const Entity &anEntity){
    //We pass you the entity so you can validate that the fields you are being asked to update are 
    //legitimate. The entity can also validate the TYPE of the data.

    //Validate the Values in KeyValues
    StatusResult result;
    result = const_cast<Entity&>(anEntity).validate(aKVList);

    KeyValues rowKVList = const_cast<Row&>(aRow).getColumns();

    if(result == StatusResult{noError}){ //Passed validation, now we can modify
      for (auto aKVPair : aKVList) {
          rowKVList[aKVPair.first] = aKVPair.second;
      }
    
      Block dummyBlock; //We are using the dummyBlock to preserve the original block's header
      result = storage.readBlock(const_cast<Row&>(aRow).getBlockNumber(), dummyBlock);

      if(result == StatusResult{noError}){ //Succesfully read block
        Block theBlock = Block(rowKVList);
        theBlock.header = dummyBlock.header;
        storage.writeBlock(const_cast<Row&>(aRow).getBlockNumber(), theBlock);
      }
    }
    
    return result;
  }

  // USE: asks the DB if a table exists in storage...
  bool Database::tableExists(const std::string aName) {
    //STUDENT: implement this if you need it...
    return false;
  }
  
}

