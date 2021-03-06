#include "EventStore.h"
#include "lcfiplus.h"

// ROOTCINT function call
#include "TMethodCall.h"
#include "TList.h"
#include "TClassTable.h"

#include <iostream>

using namespace std;

namespace lcfiplus {

// ctor/dtor
EventStore::EventStore() {}
EventStore::~EventStore() {
  // firstly clear contents of the objects
  ClearObjects();

  multimap<string, lcfiplus::EventStore::StoredEntry >::iterator itMap;
  // free all void * in the map
  for (itMap = _objectMap.begin(); itMap != _objectMap.end(); itMap ++) {
    TClass* cl = TClass::GetClass(itMap->second.classname.c_str());
    cl->Destructor(itMap->second.obj);
    itMap->second.obj = NULL;
  }
}

// check existence of the named buffer
int EventStore::Count(const char* name)const {
  return _objectMap.count(name);
}

// check existence of the named buffer
bool EventStore::IsExist(const char* name, const char* classname)const {
  int count = _objectMap.count(name);
  if (count == 0)return false;

  int n=0;
  for (n=0, _itMap = _objectMap.find(name); n<count; n++,_itMap++) {
    if (_itMap->second.classname == classname)return true;
  }
  return false;
}

// obtain class name of the named buffer
const char* EventStore::GetClassName(const char* name, int idx)const {
  int count = _objectMap.count(name);
  if (count <= idx)return NULL;
  _itMap = _objectMap.find(name);
  advance(_itMap,idx);
  return _itMap->second.classname.c_str();
}

// provide read-only buffer
void* EventStore::GetObject(const char* name, const char* classname)const {
  list<EventStoreObserver*>::const_iterator it;
  for (it = _observerList.begin(); it != _observerList.end(); it++)
    (*it)->GetCallback(name,classname);

  int count = _objectMap.count(name);
  if (count == 0)return NULL;

  _itMap = _objectMap.find(name);
  if (strlen(classname) == 0)return _itMap->second.obj;

  for (int i=0; i<count; i++,_itMap++) {
    if (_itMap->second.classname == classname)return _itMap->second.obj;
  }
  return NULL;
}

void* const& EventStore::GetObjectRef(const char* name, const char* classname)const {
  list<EventStoreObserver*>::const_iterator it;
  for (it = _observerList.begin(); it != _observerList.end(); it++)
    (*it)->GetCallback(name,classname);

  int count = _objectMap.count(name);
  if (count == 0)throw(new Exception("EventStore::GetObjectRef: Object not found"));

  _itMap = _objectMap.find(name);
  if (strlen(classname) == 0)return _itMap->second.obj;

  for (int i=0; i<count; i++,_itMap++) {
    if (_itMap->second.classname == classname)return _itMap->second.obj;
  }
  throw (new Exception("EventStore::GetObjectRef: Object not found"));
  return _itMap->second.obj;
}

// register new object
void* EventStore::RegisterObject(const char* name, const char* classname, int flags) {
  list<EventStoreObserver*>::iterator it;
  for (it = _observerList.begin(); it != _observerList.end(); it++)
    (*it)->RegisterCallback(name,classname,flags);

  // if already exists: return NULL
  int nreg = _objectMap.count(name);
  int n=0;
  if (nreg) {
    for (n=0, _itMap = _objectMap.find(name); n<nreg; n++,_itMap++) {
      if (_itMap->second.classname == classname)return NULL;
    }
  }

  // make the class
  void* newobj;
  if (classname[strlen(classname)-1] == '*') newobj = new (void*);
  else newobj = TClass::GetClass(classname)->New();

  if (!newobj)return NULL;

  // register the map
  _objectMap.insert(pair<string, StoredEntry>(name, StoredEntry(string(classname), newobj, flags)));

  cout << "EventStore::Register: collection " << name << " registered with type " << classname << endl;

  return newobj;
}

// print object map
void EventStore::Print()const {
  cout << "Printing object map in EventStore..." << endl;
  for (_itMap = _objectMap.begin(); _itMap != _objectMap.end(); _itMap++) {
    cout << "Name: " << _itMap->first << ", Class: " << _itMap->second.classname << endl;
  }
  cout << "Printing object map in EventStore finished." << endl;
}

void EventStore::ClearObjects() {
  for (_itMap = _objectMap.begin(); _itMap != _objectMap.end(); _itMap++) {
    const StoredEntry& entry = _itMap->second;

    // delete pointed objects
    if (!(entry.flag & DO_NOT_DELETE)) {
      if (entry.classname[entry.classname.length()-1] == '*') {
        // delete pointed objects of simple pointer
        string objname = entry.classname.substr(0, entry.classname.length()-1);
        TClass* cl = TClass::GetClass(objname.c_str());

        cl->Destructor(*(void**)entry.obj);
      } else if (entry.classname.find("vector") != string::npos && entry.classname.find("*") != string::npos) {
        // contain "vector" and "*": reckon as vector<T*>
        // delete pointed objects of vector elements
        // using brute method... (the pointer size is assumed to be same)

        vector<void*>* pvec = (vector<void*>*)entry.obj;
        vector<void*>::iterator it;
        for (it = pvec->begin(); it != pvec->end(); it++) {
          int nstart = entry.classname.find("<") + 1;
          int nend = entry.classname.find("*");
          string elementclassname = entry.classname.substr(nstart, nend-nstart);
          //cout << "Deleting class name: " << elementclassname << ", pointer: " << (unsigned long)*it << endl;

          TClass* cl = TClass::GetClass(elementclassname.c_str());
          if (!cl) {
            throw (Exception("EventStore::ClearObjects(); cannot get class info of element class for deletion"));
          }
          cl->Destructor(*it);
        }
      }
    } // if(!(entry.flag & DO_NOT_DELETE))

    // clearing vector elements : regardless of DO_NOT_DELETE
    if (entry.classname.find("vector") != string::npos) {
      // vector class must have clear() method
      TClass* cl = TClass::GetClass(entry.classname.c_str());

// debug code
//				gClassTable->Print();
      /*
      				cout << entry.classname << endl;
      				if(cl == 0) cout << "cl == 0" << endl;

      				cout << cl->GetListOfAllPublicMethods()->GetEntries() << endl;
      				cout << cl->GetListOfMethods()->GetEntries() << endl;
      				cout << "**method names***" << endl;
      				TIter it((TCollection *)cl->GetListOfMethods(), kIterForward);
      				while(TNamed *obj = (TNamed *)(it())){
      					cout << obj->GetName() << endl;
      				}
      				cout << "**method names***" << endl;
      */
      TMethodCall method(cl, "clear", "");
      if (method.IsValid())
        method.Execute(entry.obj);
      else
        throw (Exception("EventStore::ClearObjects(); vector clear method is not valid."));
    }
  }
}
}
