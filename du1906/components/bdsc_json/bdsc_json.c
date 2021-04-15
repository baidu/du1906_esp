/*
 * Copyright (c) 2020 Baidu.com, Inc. All Rights Reserved
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on
 * an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations under the License.
 */

#include "bdsc_json.h"

BdsJsonType   BdsJsonGetType(BdsJson* jObj)
{
    if(!jObj)
        return -1;
    return jObj->type;
}

BdsJson*   BdsJsonParse(const char* jStr)
{
    if(!jStr)
        return NULL;
    return cJSON_Parse(jStr);
}

int   BdsJsonPut(BdsJson* jObj)
{
    if(!jObj)
        return -1;
    cJSON_Delete(jObj);
    return 0;
}

BdsJson*   BdsJsonNext(BdsJson* jObj)
{
    if(!jObj)
        return NULL;
    return jObj->next;
}

BdsJson*   BdsJsonObjectNew()
{
    return cJSON_CreateObject();
}

int   BdsJsonObjectAdd(BdsJson* obj, const char* key, BdsJson* objAdd)
{
    if(!obj || !key || !objAdd){
        return -1;
    }
    cJSON_AddItemToObject(obj, key, objAdd);
    return 0;
}

int   BdsJsonObjectAddInt(BdsJson* obj, const char* key, int val)
{
    BdsJson* j;
    
    if(!obj || !key)
        return -1;
    if((j=BdsJsonIntNew(val))){
        if(BdsJsonObjectAdd(obj, key, j)){
            BdsJsonPut(j);
            return -1;
        }else{
            return 0;
        }
    }
    return -1;
}

int   BdsJsonObjectAddInt64(BdsJson* obj, const char* key, long long val)
{
    BdsJson* j;
    
    if(!obj || !key)
        return -1;
    if((j=BdsJsonInt64New(val))){
        if(BdsJsonObjectAdd(obj, key, j)){
            BdsJsonPut(j);
            return -1;
        }else{
            return 0;
        }
    }
    return -1;    
}

int   BdsJsonObjectAddDouble(BdsJson* obj, const char* key, double val)
{
    BdsJson* j;
    
    if(!obj || !key)
        return -1;
    if((j=BdsJsonDoubleNew(val))){
        if(BdsJsonObjectAdd(obj, key, j)){
            BdsJsonPut(j);
            return -1;
        }else{
            return 0;
        }
    }
    return -1; 
}

int   BdsJsonObjectAddString(BdsJson* obj, const char* key, const char* cStr)
{
    BdsJson* j;
    
    if(!obj || !key || !cStr)
        return -1;
	BdsJsonObjectDel(obj, key);
    if((j=BdsJsonStringNew(cStr))){
        if(BdsJsonObjectAdd(obj, key, j)){
            BdsJsonPut(j);
            return -1;
        }else{
            return 0;
        }
    }
    return -1;     
}

int   BdsJsonObjectAddBool(BdsJson* obj, const char* key, bool val)
{
    BdsJson* j;
    
    if(!obj || !key)
        return -1;
    if((j=BdsJsonBoolNew(val))){
        if(BdsJsonObjectAdd(obj, key, j)){
            BdsJsonPut(j);
            return -1;
        }else{
            return 0;
        }
    }
    return -1; 
}

int   BdsJsonArrayAddInt(BdsJson* obj, int val)
{
    BdsJson* j;

    if(!obj)
        return -1;
    if((j=BdsJsonIntNew(val))){
        if(BdsJsonArrayAdd(obj, j)){
            BdsJsonPut(j);
            return -1;
        }else{
            return 0;
        }
    }
    return -1;  
}

int   BdsJsonArrayAddInt64(BdsJson* obj, long long val)
{
    BdsJson* j;
    
    if(!obj)
        return -1;
    if((j=BdsJsonInt64New(val))){
        if(BdsJsonArrayAdd(obj, j)){
            BdsJsonPut(j);
            return -1;
        }else{
            return 0;
        }
    }
    return -1; 
}

int   BdsJsonArrayAddDouble(BdsJson* obj, double val)
{
    BdsJson* j;
    
    if(!obj)
        return -1;
    if((j=BdsJsonDoubleNew(val))){
        if(BdsJsonArrayAdd(obj, j)){
            BdsJsonPut(j);
            return -1;
        }else{
            return 0;
        }
    }
    return -1; 
}

int   BdsJsonArrayAddString(BdsJson* obj, const char* cStr)
{
    BdsJson* j;
    
    if(!obj || !cStr)
        return -1;
    if((j=BdsJsonStringNew(cStr))){
        if(BdsJsonArrayAdd(obj, j)){
            BdsJsonPut(j);
            return -1;
        }else{
            return 0;
        }
    }
    return -1; 
}

int   BdsJsonArrayAddBool(BdsJson* obj, bool val)
{
    BdsJson* j;
    
    if(!obj)
        return -1;
    if((j=BdsJsonBoolNew(val))){
        if(BdsJsonArrayAdd(obj, j)){
            BdsJsonPut(j);
            return -1;
        }else{
            return 0;
        }
    }
    return -1; 
}

int   BdsJsonArrayDel(BdsJson* obj, BdsJson* toDel)
{
    int i=0;
    BdsJson* tmpObj;
    
    while (1) {
        if (!(tmpObj = BdsJsonArrayGet(obj, i))) {
            return -1;
        }
        
        if (tmpObj == toDel) {
            cJSON_DeleteItemFromArray(obj, i);
            return 0;
        }
        i++;
    }
}

int   BdsJsonObjectDel(BdsJson* obj, const char* key)
{
    cJSON_DeleteItemFromObject(obj, key);
    return 0;
}

BdsJson*   BdsJsonObjectGet(BdsJson* jObj, const char* key)
{
    if(!jObj)
        return NULL;
    if(jObj->type != cJSON_Object){
        return NULL;
    }
    BdsJsonForeach(jObj, tmpObj){
        if(tmpObj->string && !strcmp(tmpObj->string, key)){
            return tmpObj;
        }
    }
    return NULL;
}

BdsJson*   BdsJsonStringNew(const char* cStr)
{
    if(!cStr){
        return NULL;
    }
    return cJSON_CreateString(cStr);
}

char*   BdsJsonStringGet(BdsJson* jObj)
{
    if(!jObj)
        return NULL;
    if(jObj->type == cJSON_String && jObj->valuestring)
        return jObj->valuestring;
     return NULL;
}

BdsJson*   BdsJsonIntNew(int num)
{
    return cJSON_CreateNumber((double)num);
}

BdsJson*   BdsJsonInt64New(long long num)
{
    return cJSON_CreateNumber((double)num);
}

int   BdsJsonIntGet(BdsJson* jObj, int* value)
{
    if(!jObj)
        return -1;
    if(jObj->type == cJSON_Number){
        *value = jObj->valueint;
        return 0;
    }
    return -1;
}

BdsJson*   BdsJsonDoubleNew(double num)
{
    return cJSON_CreateNumber(num);
}

int   BdsJsonDoubleGet(BdsJson* jObj, double* value)
{
    if(!jObj)
        return -1;
    if(jObj->type == cJSON_Number){
        *value = jObj->valuedouble;
        return 0;
    }
    return -1;
}

BdsJson*   BdsJsonBoolNew(bool bl)
{
    if(bl!=true && bl!=false)
        return NULL;
    return cJSON_CreateBool(bl);
}

int   BdsJsonBoolGet(BdsJson* jObj, bool* bl)
{
    if(!jObj)
        return -1;
    if(jObj->type == cJSON_False){
        *bl = false;
        return 0;
    }else if(jObj->type == cJSON_True){
        *bl = true;
        return 0;
    }
    return -1;    
}

char*   BdsJsonObjectGetString(BdsJson* jObj, const char* key)
{
    BdsJson* obj;

    if(!jObj)
        return NULL;
    if((obj=BdsJsonObjectGet(jObj, key)))
        return BdsJsonStringGet(obj);
    return NULL;
}

int   BdsJsonObjectGetInt(BdsJson* jObj, const char* key, int* value)
{
    BdsJson* obj;

    if(!jObj)
        return -1;
    if((obj=BdsJsonObjectGet(jObj, key)))
        return BdsJsonIntGet(obj, value);
    return -1;
}

int   BdsJsonObjectGetDouble(BdsJson* jObj, const char* key, double* value)
{
    BdsJson* obj;
    
    if(!jObj)
        return -1;
    if((obj=BdsJsonObjectGet(jObj, key)))
        return BdsJsonDoubleGet(obj, value);
    return -1;
}

int   BdsJsonObjectGetBool(BdsJson* jObj, const char* key, bool* value)
{
    BdsJson* obj;
    
    if(!jObj)
        return -1;
    if((obj=BdsJsonObjectGet(jObj, key)))
        return BdsJsonBoolGet(obj, value);
    return -1;    
}

BdsJson*   BdsJsonArrayNew()
{
    return cJSON_CreateArray();
}

int   BdsJsonArrayAdd(BdsJson* array, BdsJson* obj)
{
    if(!array || !obj)
        return -1;
    cJSON_AddItemToArray(array, obj);
    return 0;
}

int   BdsJsonArrayLen(BdsJson* jObj)
{
    if(!jObj)
        return -1;
    if(jObj->type != cJSON_Array)
        return -1;
    return cJSON_GetArraySize(jObj);
}

BdsJson*   BdsJsonArrayGet(BdsJson* jObj, int idx)
{
    int i=0;

    if(!jObj)
        return NULL;
    if(jObj->type == cJSON_Array){
        BdsJsonForeach(jObj, tmpObj){
            if(i==idx) {
                return (tmpObj);
            }
            i++;
        }
    }
    return NULL;
}

const char*   BdsJsonArrayGetString(BdsJson* jObj, int idx)
{
    int i=0;
    
    if(!jObj)
        return NULL;
    if(jObj->type == cJSON_Array){
        BdsJsonForeach(jObj, tmpObj){
            if(i==idx) {
                return BdsJsonStringGet(tmpObj);
            }
            i++;
        }
    }
    return NULL;
}

int   BdsJsonArrayGetInt(BdsJson* jObj, int idx, int* value)
{
    int i=0;
    
    if(!jObj)
        return -1;
    if(jObj->type == cJSON_Array){
        BdsJsonForeach(jObj, tmpObj){
            if(i==idx) {
                return BdsJsonIntGet(tmpObj, value);
            }
            i++;
        }
    }
    return -1;
}

int   BdsJsonArrayGetDouble(BdsJson* jObj, int idx, double* value)
{
    int i=0;
    
    if(!jObj)
        return -1;
    if(jObj->type == cJSON_Array){
        BdsJsonForeach(jObj, tmpObj){
            if(i==idx) {
                return BdsJsonDoubleGet(tmpObj, value);
            }
            i++;
        }
    }
    return -1;
}

char*   BdsJsonNewCStr(BdsJson* jObj)
{
    if(!jObj)
        return NULL;
    return cJSON_Print(jObj);
}

char*   BdsJsonPrint(BdsJson* jObj)
{
    return cJSON_Print(jObj);
}

char*   BdsJsonPrintUnformatted(BdsJson* jObj)
{
    return cJSON_PrintUnformatted(jObj);
}

