#include "jvmti.h"
#include "jni.h"
#include <string.h> /* memset */
#include <unistd.h> /* close */
#include <stdlib.h>
#include <stdbool.h>

char* clazz;
char* method;
char* param;
char* ret;
jmethodID method_id = NULL;
bool instance = false;
int thiz_offset;

jmethodID to_string_method_id = NULL;

void JNICALL OnVMInit(jvmtiEnv *jvmti, JNIEnv* jni, jthread thread)
{
	jclass jclazz = (*jni)->FindClass(jni, clazz);
	char* signature = (char*) malloc(3 + strlen(param)+ strlen(ret));
	strcpy(signature, "(");
	strcat(signature, param);
	strcat(signature, ")");
	strcat(signature, ret);
	method_id = (*jni)->GetStaticMethodID(jni, jclazz, method, signature);
	if(method_id == NULL)
	{	
		(*jni)->ExceptionClear(jni);
		method_id = (*jni)->GetMethodID(jni, jclazz, method, signature);
		instance = true;
		thiz_offset = 1;
	}

	free(signature);
}

char* ToString(JNIEnv* jni, jvalue value, char* type) {
	const size_t size = 128;
	char* string = (char*) malloc(sizeof(char) * size);
	switch(type[0]) {
		case 'B': snprintf(string, 128, "%dB", return_value.b); break;
		case 'S': snprintf(string, 128, "%dS", return_value.s); break;
		case 'I': snprintf(string, 128, "%d", return_value.i); break;
		case 'J': snprintf(string, 128, "%ldL", return_value.j); break;
		case 'F': snprintf(string, 128, "%fF", return_value.f); break;
		case 'D': snprintf(string, 128, "%lfD", return_value.d); break;
		case 'Z': snprintf(string, 128, "%s", return_value.z ? "true" : "false"); break;
		case 'C': snprintf(string, 128, "\'%c\'", return_value.c); break;
		case 'L': case '[':
			jstring toString = (jstring) (*jni)->CallObjectMethod(jni, value.object, to_string_method_id);
			const char* toStringChars = (*jni)->GetStringUTFChars(jni, toString, NULL);
			snprintf(string, 128, "%s", toStringChars);
			(*jni)->ReleaseUTFChars(jni, toString);
			break;
		default: string[0] = '\0';
	}
}

void PrintArgument(jvmtiEnv *jvmti, JNIEnv* jni, jthread thread, jvmtiLocalVariableEntry* table, int i, int* offset){

	int index = i+*offset-thiz_offset;
	char* type = param + index;
	jvalue value;
	switch(param[index]) {
		case 'Z': (*jvmti)->GetLocalInt(jvmti, thread, 0, table[i].slot, (jint*) &value.z); *offset++; break;
		case 'C': (*jvmti)->GetLocalInt(jvmti, thread, 0, table[i].slot, (jint*) &value.c); *offset++; break;
		case 'B': (*jvmti)->GetLocalInt(jvmti, thread, 0, table[i].slot, (jint*) &value.b); *offset++; break;
		case 'S': (*jvmti)->GetLocalInt(jvmti, thread, 0, table[i].slot, (jint*) &value.s); *offset++; break;
		case 'I': (*jvmti)->GetLocalInt(jvmti, thread, 0, table[i].slot, (jint*) &value.i); *offset++; break;
		case 'J': (*jvmti)->GetLocalLong(jvmti, thread, 0, table[i].slot, (jlong*) &value.j); *offset++; break;
		case 'F': (*jvmti)->GetLocalFloat(jvmti, thread, 0, table[i].slot, (jfloat*) &value.f); *offset++; break;
		case 'D': (*jvmti)->GetLocalDouble(jvmti, thread, 0, table[i].slot, (jdouble*) &value.d); *offset++; break;
		case 'L': case '[': {
			(*jvmti)->GetLocalObject(jvmti, thread, 0, table[i].slot, (jobject*) &value.object);
			char* off = &param[i];
			char* start;
			if(param[index] == 'L') {
				start = strchr(off, 'L');
			} else {
				start = strchr(off, '[');
			}
			char* end = strchr(off, ';');
			int diff = end-start;		
	
			if(instance) {
				jclass jclazz = (*jni)->FindClass(jni, clazz);
				jmethodID jmethod = (*jni)->GetMethodID(jni, jclazz, "toString", "()Ljava/lang/String;");
				jstring jstr = (jstring) (*jni)->CallObjectMethod(jni, var, jmethod);
				const char* str = (*jni)->GetStringUTFChars(jni, jstr, NULL);
				fprintf(stdout, "%s", (char*) str);
			} else  {
				char* clazz_name = (char*) malloc(diff);
				strncpy(clazz_name, off, diff);
				fprintf(stdout, "%s", clazz_name);
				free(clazz_name);
			}
			*offset += diff;
			break;
		}
	}
}

void PrintMethod(jvmtiEnv *jvmti, JNIEnv* jni, jthread thread)
{
	jint entry_count;
	jvmtiLocalVariableEntry* table;

	(*jvmti)->GetLocalVariableTable(jvmti, method_id, &entry_count, &table);
	fprintf(stdout, "%s.%s(", clazz, method);

	// traverse arguments
	int offset = 0;
	for(int i=0; i<entry_count; i++)
	{	
			
		if(i!=0) fprintf(stdout, ", ");
		else 
		{
			if(instance)	i++; // this pointer
		}
		PrintArgument(jvmti, jni, thread, table, i, &offset);
	}

	fprintf(stdout, ")");
	fflush(stdout);

	// deallocate
	for(int i=0; i<entry_count; i++)
	{	
		(*jvmti)->Deallocate(jvmti, (unsigned char*) table[i].name);
		(*jvmti)->Deallocate(jvmti, (unsigned char*) table[i].signature);
		if (table[i].generic_signature != NULL) (*jvmti)->Deallocate(jvmti, (unsigned char*) table[i].generic_signature);
	}
	
	(*jvmti)->Deallocate(jvmti, (unsigned char*) table);
}

void JNICALL OnMethodEntry(jvmtiEnv *jvmti, JNIEnv* jni, jthread thread, jmethodID jmethod)
{
	if(jmethod == method_id)
	{
		PrintMethod(jvmti, jni, thread);	
		fprintf(stdout, " \n");
	}
	
}

void JNICALL OnMethodExit(jvmtiEnv *jvmti, JNIEnv* jni, jthread thread, jmethodID method, jboolean was_popped_by_exception, jvalue return_value)
{
	if(method == method_id)
	{
		PrintMethod(jvmti, jni, thread);
		fprintf(stdout, " =>");
		switch(ret[0]) {
			case "B": fprintf("%d", return_value.b); break;
			case "S": fprintf("%d", return_value.s); break;
			case "I": fprintf("%d", return_value.i); break;
			case "J": fprintf("%ld", return_value.j); break;
			case "I": fprintf("%f", return_value.f); break;
			case "J": fprintf("%lf", return_value.d); break;
			case "Z": fprintf("%s", return_value.z ? "true" : "false"); break;
			case "C": fprintf("%c", return_value.c); break;
		}
		fflush(stdout);
	}
}

JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM* _vm, char* options, void* reserved) 
{
	jvmtiEnv* jvmti = NULL;
	(*_vm)->GetEnv(_vm, (void**) &jvmti, JVMTI_VERSION);

	
	if (options != NULL && options[0] != '\0') {
		char* nextArgument = options;
		const char delimiters[] = ":=";
		char *key, *value;
		int i = 0;

		do {
			if(i==0) key = strtok(nextArgument, delimiters);
			else key = strtok(NULL, delimiters);

			if(key != NULL){
				value = strtok(NULL, delimiters);
				if(strstr(key, "clazz")!=NULL) clazz = value;
				else if(strstr(key, "method")!=NULL) method = value;
				else if(strstr(key, "param")!=NULL) param = value;
				else if(strstr(key, "ret")!=NULL) ret = value;
			}

			i++;
		} while(key != NULL); 
	}


 	jvmtiError error;
	jvmtiCapabilities requestedCapabilities, potentialCapabilities;
	memset(&requestedCapabilities, 0, sizeof(requestedCapabilities));

	// error checks
	if((error = (*jvmti)->GetPotentialCapabilities(jvmti, &potentialCapabilities)) != JVMTI_ERROR_NONE) 			
	return 0;

	if(potentialCapabilities.can_generate_method_entry_events) 
	{
	       requestedCapabilities.can_generate_method_entry_events = 1;
	}
	if(potentialCapabilities.can_generate_method_exit_events)
	{
		requestedCapabilities.can_generate_method_exit_events = 1;
	}
 	if(potentialCapabilities.can_access_local_variables)
	{
		requestedCapabilities.can_access_local_variables = 1;
	}

	// enable method entry and exit capabilities
	if((error = (*jvmti)->AddCapabilities(jvmti, &requestedCapabilities)) != JVMTI_ERROR_NONE) return 0;


	jvmtiEventCallbacks callbacks;
	memset(&callbacks, 0, sizeof(callbacks));
	callbacks.VMInit = OnVMInit;
	callbacks.MethodEntry = OnMethodEntry;
	callbacks.MethodExit = OnMethodExit;

	(*jvmti)->SetEventCallbacks(jvmti, &callbacks, sizeof(callbacks));
	(*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE, JVMTI_EVENT_VM_INIT, NULL);
	(*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE, JVMTI_EVENT_METHOD_ENTRY, NULL);
	(*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE, JVMTI_EVENT_METHOD_EXIT, NULL);

	return 0;
}