/** 
 * XMLSec library
 *
 * "XML Encryption" implementation
 *  http://www.w3.org/TR/xmlenc-core
 * 
 * See Copyright for the status of this software.
 * 
 * Author: Aleksey Sanin <aleksey@aleksey.com>
 */
#include "globals.h"

#ifndef XMLSEC_NO_XMLENC
 
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <libxml/tree.h>
#include <libxml/parser.h> 

#include <xmlsec/xmlsec.h>
#include <xmlsec/buffer.h>
#include <xmlsec/xmltree.h>
#include <xmlsec/keys.h>
#include <xmlsec/keysmngr.h>
#include <xmlsec/transforms.h>
#include <xmlsec/keyinfo.h>
#include <xmlsec/xmlenc.h>
#include <xmlsec/errors.h>

static int 	xmlSecEncCtxEncDataNodeRead		(xmlSecEncCtxPtr ctx, 
							 xmlNodePtr node);
static int 	xmlSecEncCtxCipherDataNodeRead		(xmlSecEncCtxPtr ctx, 
							 xmlNodePtr node);
static int 	xmlSecEncCtxCipherDataNodeWrite		(xmlSecEncCtxPtr ctx);
static int 	xmlSecEncCtxCipherReferenceNodeRead	(xmlSecEncCtxPtr ctx, 
							 xmlNodePtr node);

/* The ID attribute in XMLEnc is 'Id' */
static const xmlChar*		xmlSecEncIds[] = { BAD_CAST "Id", NULL };


xmlSecEncCtxPtr	
xmlSecEncCtxCreate(xmlSecKeysMngrPtr keysMngr) {
    xmlSecEncCtxPtr ctx;
    int ret;
    
    ctx = (xmlSecEncCtxPtr) xmlMalloc(sizeof(xmlSecEncCtx));
    if(ctx == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    NULL,
		    "xmlMalloc",
		    XMLSEC_ERRORS_R_MALLOC_FAILED,
		    "sizeof(xmlSecEncCtx)=%d", 
		    sizeof(xmlSecEncCtx));
	return(NULL);
    }
    
    ret = xmlSecEncCtxInitialize(ctx, keysMngr);
    if(ret < 0) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    NULL,
		    "xmlSecEncCtxInitialize",
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    XMLSEC_ERRORS_NO_MESSAGE);
	xmlSecEncCtxDestroy(ctx);
	return(NULL);   
    }
    return(ctx);    
}

void  
xmlSecEncCtxDestroy(xmlSecEncCtxPtr ctx) {
    xmlSecAssert(ctx != NULL);
    
    xmlSecEncCtxFinalize(ctx);
    xmlFree(ctx);
}

int 
xmlSecEncCtxInitialize(xmlSecEncCtxPtr ctx, xmlSecKeysMngrPtr keysMngr) {
    int ret;
    
    xmlSecAssert2(ctx != NULL, -1);
    
    memset(ctx, 0, sizeof(xmlSecEncCtx));

    /* initialize key info */
    ret = xmlSecKeyInfoCtxInitialize(&(ctx->keyInfoReadCtx), keysMngr);
    if(ret < 0) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    NULL,
		    "xmlSecKeyInfoCtxInitialize",
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    XMLSEC_ERRORS_NO_MESSAGE);
	return(-1);   
    }
    ctx->keyInfoReadCtx.mode = xmlSecKeyInfoModeRead;
    
    ret = xmlSecKeyInfoCtxInitialize(&(ctx->keyInfoWriteCtx), keysMngr);
    if(ret < 0) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    NULL,
		    "xmlSecKeyInfoCtxInitialize",
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    XMLSEC_ERRORS_NO_MESSAGE);
	return(-1);   
    }
    ctx->keyInfoWriteCtx.mode = xmlSecKeyInfoModeWrite;
    /* it's not wise to write private key :) */
    ctx->keyInfoWriteCtx.keyReq.keyType = xmlSecKeyDataTypePublic;

    /* initializes transforms ctx */
    ret = xmlSecTransformCtxInitialize(&(ctx->encTransformCtx));
    if(ret < 0) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    NULL,
		    "xmlSecTransformCtxInitialize",
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    XMLSEC_ERRORS_NO_MESSAGE);
	return(-1);   
    }
	    
    return(0);
}

void 
xmlSecEncCtxFinalize(xmlSecEncCtxPtr ctx) {
    xmlSecAssert(ctx != NULL);

    xmlSecTransformCtxFinalize(&(ctx->encTransformCtx));
    xmlSecKeyInfoCtxFinalize(&(ctx->keyInfoReadCtx));
    xmlSecKeyInfoCtxFinalize(&(ctx->keyInfoWriteCtx));

    if((ctx->dontDestroyEncMethod != 0) && (ctx->encMethod != NULL)) {
	xmlSecTransformDestroy(ctx->encMethod, 1);
    }    
    if(ctx->encKey != NULL) {
	xmlSecKeyDestroy(ctx->encKey);
    }
    if(ctx->id != NULL) {
	xmlFree(ctx->id);
    }	
    if(ctx->type != NULL) {
	xmlFree(ctx->type);
    }
    if(ctx->mimeType != NULL) {
	xmlFree(ctx->mimeType);
    }
    if(ctx->encoding != NULL) {
	xmlFree(ctx->encoding);
    }	
    if(ctx->recipient != NULL) {
	xmlFree(ctx->recipient);
    }
    if(ctx->carriedKeyName != NULL) {
	xmlFree(ctx->carriedKeyName);
    }
    
    memset(ctx, 0, sizeof(xmlSecEncCtx));
}

int 
xmlSecEncCtxBinaryEncrypt(xmlSecEncCtxPtr ctx, xmlNodePtr tmpl, 
			  const unsigned char* data, size_t dataSize) {
    int ret;
    
    xmlSecAssert2(ctx != NULL, -1);
    xmlSecAssert2(ctx->encResult == NULL, -1);
    xmlSecAssert2(tmpl != NULL, -1);
    xmlSecAssert2(data != NULL, -1);

    /* initialize context and add ID atributes to the list of known ids */    
    ctx->encrypt = 1;
    xmlSecAddIDs(tmpl->doc, tmpl, xmlSecEncIds);

    /* read the template and set encryption method, key, etc. */
    ret = xmlSecEncCtxEncDataNodeRead(ctx, tmpl);
    if(ret < 0) {
    	xmlSecError(XMLSEC_ERRORS_HERE,
		    NULL,
		    "xmlSecEncCtxEncDataNodeRead",
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    XMLSEC_ERRORS_NO_MESSAGE);
	return(-1);
    }

    ret = xmlSecTransformCtxBinaryExecute(&(ctx->encTransformCtx), data, dataSize);
    if(ret < 0) {
    	xmlSecError(XMLSEC_ERRORS_HERE,
		    NULL,
		    "xmlSecTransformCtxBinaryExecute",
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "dataSize=%d",
		    dataSize);
	return(-1);
    }

    ctx->encResult = ctx->encTransformCtx.result;
    xmlSecAssert2(ctx->encResult != NULL, -1);
    
    ret = xmlSecEncCtxCipherDataNodeWrite(ctx);
    if(ret < 0) {
    	xmlSecError(XMLSEC_ERRORS_HERE,
		    NULL,
		    "xmlSecEncCtxCipherDataNodeWrite",
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    XMLSEC_ERRORS_NO_MESSAGE);
	return(-1);
    }
    return(0);    
}


int 
xmlSecEncCtxXmlEncrypt(xmlSecEncCtxPtr ctx, xmlNodePtr tmpl, xmlNodePtr node) {
    xmlOutputBufferPtr output;
    int ret;
    
    xmlSecAssert2(ctx != NULL, -1);
    xmlSecAssert2(ctx->encResult == NULL, -1);
    xmlSecAssert2(tmpl != NULL, -1);
    xmlSecAssert2(node != NULL, -1);
    xmlSecAssert2(node->doc != NULL, -1);

    /* initialize context and add ID atributes to the list of known ids */    
    ctx->encrypt = 1;
    xmlSecAddIDs(tmpl->doc, tmpl, xmlSecEncIds);

    /* read the template and set encryption method, key, etc. */
    ret = xmlSecEncCtxEncDataNodeRead(ctx, tmpl);
    if(ret < 0) {
    	xmlSecError(XMLSEC_ERRORS_HERE,
		    NULL,
		    "xmlSecEncCtxEncDataNodeRead",
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    XMLSEC_ERRORS_NO_MESSAGE);
	return(-1);
    }

    ret = xmlSecTransformCtxPrepare(&(ctx->encTransformCtx), xmlSecTransformDataTypeBin);
    if(ret < 0) {
    	xmlSecError(XMLSEC_ERRORS_HERE,
		    NULL,
		    "xmlSecTransformCtxPrepare",
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "type=bin");
	return(-1);
    }
    
    xmlSecAssert2(ctx->encTransformCtx.first != NULL, -1);
    output = xmlSecTransformCreateOutputBuffer(ctx->encTransformCtx.first, 
						&(ctx->encTransformCtx));
    if(output == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    xmlSecErrorsSafeString(xmlSecTransformGetName(ctx->encTransformCtx.first)),
		    "xmlSecTransformCreateOutputBuffer",
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    XMLSEC_ERRORS_NO_MESSAGE);
	return(-1);
    }

    /* push data thru */
    if((ctx->type != NULL) && xmlStrEqual(ctx->type, xmlSecTypeEncElement)) {
	/* get the content of the node */
	xmlNodeDumpOutput(output, node->doc, node, 0, 0, NULL);
    } else if((ctx->type != NULL) && xmlStrEqual(ctx->type, xmlSecTypeEncContent)) {
	xmlNodePtr cur;

	/* get the content of the nodes childs */
	for(cur = node->children; cur != NULL; cur = cur->next) {
	    xmlNodeDumpOutput(output, node->doc, cur, 0, 0, NULL);
	}
    } else {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    NULL,
		    NULL,
		    XMLSEC_ERRORS_R_INVALID_TYPE,
		    "type=\"%s\"", 
		    xmlSecErrorsSafeString(ctx->type));
	xmlOutputBufferClose(output);
	return(-1);	    	
    }
    
    /* close the buffer and flush everything */
    xmlOutputBufferClose(output);

    ctx->encResult = ctx->encTransformCtx.result;
    xmlSecAssert2(ctx->encResult != NULL, -1);
    
    ret = xmlSecEncCtxCipherDataNodeWrite(ctx);
    if(ret < 0) {
    	xmlSecError(XMLSEC_ERRORS_HERE,
		    NULL,
		    "xmlSecEncCtxCipherDataNodeWrite",
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    XMLSEC_ERRORS_NO_MESSAGE);
	return(-1);
    }
    
    /* now we need to update our original document */
    if((ctx->type != NULL) && xmlStrEqual(ctx->type, xmlSecTypeEncElement)) {
	ret = xmlSecReplaceNode(node, tmpl);
	if(ret < 0) {
	    xmlSecError(XMLSEC_ERRORS_HERE,
			NULL,
			"xmlSecReplaceNode",
			XMLSEC_ERRORS_R_XMLSEC_FAILED,
			"node=%s",
			xmlSecErrorsSafeString(xmlSecNodeGetName(node)));
	    return(-1);
	}
	ctx->replaced = 1;			       
    } else if(xmlStrEqual(ctx->type, xmlSecTypeEncContent)) {
	ret = xmlSecReplaceContent(node, tmpl);
	if(ret < 0) {
	    xmlSecError(XMLSEC_ERRORS_HERE,
			NULL,
			"xmlSecReplaceContent",
			XMLSEC_ERRORS_R_XMLSEC_FAILED,
			"node=%s",
			xmlSecErrorsSafeString(xmlSecNodeGetName(node)));
	    return(-1);
	}
	ctx->replaced = 1;			       
    } else {
	/* we should've catached this error before */
	xmlSecError(XMLSEC_ERRORS_HERE,
		    NULL,
		    NULL,
		    XMLSEC_ERRORS_R_INVALID_TYPE,
		    "type=\"%s\"", 
		    xmlSecErrorsSafeString(ctx->type));
	return(-1);	    	
    }
    return(0);    
}

int 
xmlSecEncCtxUriEncrypt(xmlSecEncCtxPtr ctx, xmlNodePtr tmpl, const xmlChar *uri) {
    int ret;
    
    xmlSecAssert2(ctx != NULL, -1);
    xmlSecAssert2(ctx->encResult == NULL, -1);
    xmlSecAssert2(tmpl != NULL, -1);
    xmlSecAssert2(uri != NULL, -1);

    /* initialize context and add ID atributes to the list of known ids */    
    ctx->encrypt = 1;
    xmlSecAddIDs(tmpl->doc, tmpl, xmlSecEncIds);

    /* we need to add input uri transform first */
    ret = xmlSecTransformCtxSetUri(&(ctx->encTransformCtx), uri, tmpl);
    if(ret < 0) {
    	xmlSecError(XMLSEC_ERRORS_HERE,
		    NULL,
		    "xmlSecTransformCtxSetUri",
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "uri=%s",
		    uri);
	return(-1);
    }

    /* read the template and set encryption method, key, etc. */
    ret = xmlSecEncCtxEncDataNodeRead(ctx, tmpl);
    if(ret < 0) {
    	xmlSecError(XMLSEC_ERRORS_HERE,
		    NULL,
		    "xmlSecEncCtxEncDataNodeRead",
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    XMLSEC_ERRORS_NO_MESSAGE);
	return(-1);
    }

    /* encrypt the data */
    ret = xmlSecTransformCtxExecute(&(ctx->encTransformCtx), tmpl->doc);
    if(ret < 0) {
    	xmlSecError(XMLSEC_ERRORS_HERE,
		    NULL,
		    "xmlSecTransformCtxExecute",
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    XMLSEC_ERRORS_NO_MESSAGE);
	return(-1);
    }
        
    ctx->encResult = ctx->encTransformCtx.result;
    xmlSecAssert2(ctx->encResult != NULL, -1);
    
    ret = xmlSecEncCtxCipherDataNodeWrite(ctx);
    if(ret < 0) {
    	xmlSecError(XMLSEC_ERRORS_HERE,
		    NULL,
		    "xmlSecEncCtxCipherDataNodeWrite",
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    XMLSEC_ERRORS_NO_MESSAGE);
	return(-1);
    }    
    
    return(0);
}

int 
xmlSecEncCtxDecrypt(xmlSecEncCtxPtr ctx, xmlNodePtr node) {
    xmlSecBufferPtr buffer;
    int ret;
    
    xmlSecAssert2(ctx != NULL, -1);
    xmlSecAssert2(node != NULL, -1);
    
    /* decrypt */
    buffer = xmlSecEncCtxDecryptToBuffer(ctx, node);
    if(buffer == NULL) {
    	xmlSecError(XMLSEC_ERRORS_HERE,
		    NULL,
		    "xmlSecEncCtxDecryptToBuffer",
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    XMLSEC_ERRORS_NO_MESSAGE);
	return(-1);
    }
    
    /* replace original node if requested */
    if((ctx->type != NULL) && xmlStrEqual(ctx->type, xmlSecTypeEncElement)) {
	ret = xmlSecReplaceNodeBuffer(node, xmlSecBufferGetData(buffer),  xmlSecBufferGetSize(buffer));
	if(ret < 0) {
	    xmlSecError(XMLSEC_ERRORS_HERE,
			NULL,
			"xmlSecReplaceNodeBuffer",
			XMLSEC_ERRORS_R_XMLSEC_FAILED,
			"node=%s",
			xmlSecErrorsSafeString(xmlSecNodeGetName(node)));
	    return(-1);	    	
	}
	ctx->replaced = 1;			       
    } else if((ctx->type != NULL) && xmlStrEqual(ctx->type, xmlSecTypeEncContent)) {
	/* replace the node with the buffer */
	ret = xmlSecReplaceNodeBuffer(node, xmlSecBufferGetData(buffer), xmlSecBufferGetSize(buffer));
	if(ret < 0) {
	    xmlSecError(XMLSEC_ERRORS_HERE,
			NULL,
			"xmlSecReplaceNodeBuffer",
			XMLSEC_ERRORS_R_XMLSEC_FAILED,
			"node=%s",
			xmlSecErrorsSafeString(xmlSecNodeGetName(node)));
	    return(-1);	    	
	}	
	ctx->replaced = 1;			       
    }
    return(0);
}

xmlSecBufferPtr
xmlSecEncCtxDecryptToBuffer(xmlSecEncCtxPtr ctx, xmlNodePtr node) {
    int ret;
    
    xmlSecAssert2(ctx != NULL, NULL);
    xmlSecAssert2(ctx->encResult == NULL, NULL);
    xmlSecAssert2(node != NULL, NULL);

    /* initialize context and add ID atributes to the list of known ids */    
    ctx->encrypt = 0;
    xmlSecAddIDs(node->doc, node, xmlSecEncIds);

    ret = xmlSecEncCtxEncDataNodeRead(ctx, node);
    if(ret < 0) {
    	xmlSecError(XMLSEC_ERRORS_HERE,
		    NULL,
		    "xmlSecEncCtxEncDataNodeRead",
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    XMLSEC_ERRORS_NO_MESSAGE);
	return(NULL);
    }

    /* decrypt the data */
    if(ctx->cipherValueNode != NULL) {
        xmlChar* data = NULL;
        size_t dataSize = 0;

	data = xmlNodeGetContent(ctx->cipherValueNode);
	if(data == NULL) {
	    xmlSecError(XMLSEC_ERRORS_HERE,
			NULL,
			"xmlNodeGetContent",
			XMLSEC_ERRORS_R_INVALID_NODE_CONTENT,
			"node=%s",
			xmlSecErrorsSafeString(xmlSecNodeGetName(ctx->cipherValueNode)));
	    return(NULL);
	}	
	dataSize = xmlStrlen(data);

        ret = xmlSecTransformCtxBinaryExecute(&(ctx->encTransformCtx), data, dataSize);
	if(ret < 0) {
    	    xmlSecError(XMLSEC_ERRORS_HERE,
			NULL,
			"xmlSecTransformCtxBinaryExecute",
			XMLSEC_ERRORS_R_XMLSEC_FAILED,
			XMLSEC_ERRORS_NO_MESSAGE);
	    if(data != NULL) {
		xmlFree(data);
	    }
	    return(NULL);
	}
	if(data != NULL) {
	    xmlFree(data);
	}
    } else {
        ret = xmlSecTransformCtxExecute(&(ctx->encTransformCtx), node->doc);
	if(ret < 0) {
    	    xmlSecError(XMLSEC_ERRORS_HERE,
			NULL,
			"xmlSecTransformCtxBinaryExecute",
			XMLSEC_ERRORS_R_XMLSEC_FAILED,
			XMLSEC_ERRORS_NO_MESSAGE);
	    return(NULL);
	}
    }
    
    ctx->encResult = ctx->encTransformCtx.result;
    xmlSecAssert2(ctx->encResult != NULL, NULL);
    
    return(ctx->encResult);
}

static int 
xmlSecEncCtxEncDataNodeRead(xmlSecEncCtxPtr ctx, xmlNodePtr node) {
    xmlNodePtr cur;
    int ret;
    
    xmlSecAssert2(ctx != NULL, -1);
    xmlSecAssert2(node != NULL, -1);
    
    /* first read node data */
    xmlSecAssert2(ctx->id == NULL, -1);
    xmlSecAssert2(ctx->type == NULL, -1);
    xmlSecAssert2(ctx->mimeType == NULL, -1);
    xmlSecAssert2(ctx->encoding == NULL, -1);
    xmlSecAssert2(ctx->recipient == NULL, -1);
    xmlSecAssert2(ctx->carriedKeyName == NULL, -1);
    
    ctx->id = xmlGetProp(node, xmlSecAttrId);
    ctx->type = xmlGetProp(node, xmlSecAttrType);
    ctx->mimeType = xmlGetProp(node, xmlSecAttrMimeType);
    ctx->encoding = xmlGetProp(node, xmlSecAttrEncoding);    
    if(ctx->mode == xmlEncCtxModeEncryptedKey) {
	ctx->recipient = xmlGetProp(node, xmlSecAttrRecipient);    
	/* todo: check recipient? */
    }
    cur = xmlSecGetNextElementNode(node->children);
    
    /* first node is optional EncryptionMethod, we'll read it later */
    xmlSecAssert2(ctx->encMethodNode == NULL, -1);
    if((cur != NULL) && (xmlSecCheckNodeName(cur, xmlSecNodeEncryptionMethod, xmlSecEncNs))) {
	ctx->encMethodNode = cur;
        cur = xmlSecGetNextElementNode(cur->next);
    }

    /* next node is optional KeyInfo, we'll process it later */
    xmlSecAssert2(ctx->keyInfoNode == NULL, -1);
    if((cur != NULL) && (xmlSecCheckNodeName(cur, xmlSecNodeKeyInfo, xmlSecDSigNs))) {
	ctx->keyInfoNode = cur;
	cur = xmlSecGetNextElementNode(cur->next);
    }    

    /* next is required CipherData node */
    if((cur == NULL) || (!xmlSecCheckNodeName(cur, xmlSecNodeCipherData, xmlSecEncNs))) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    NULL,
		    xmlSecErrorsSafeString(xmlSecNodeGetName(cur)),
		    XMLSEC_ERRORS_R_INVALID_NODE,
		    "node=%s",
		    xmlSecErrorsSafeString(xmlSecNodeCipherData));
	return(-1);
    }
    
    ret = xmlSecEncCtxCipherDataNodeRead(ctx, cur);
    if(ret < 0) {
        xmlSecError(XMLSEC_ERRORS_HERE,
		    NULL,
		    "xmlSecEncCtxCipherDataNodeRead",
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    XMLSEC_ERRORS_NO_MESSAGE);
	return(-1);
    }
    cur = xmlSecGetNextElementNode(cur->next);

    /* next is optional EncryptionProperties node (we simply ignore it) */
    if((cur != NULL) && (xmlSecCheckNodeName(cur, xmlSecNodeEncryptionProperties, xmlSecEncNs))) {
	cur = xmlSecGetNextElementNode(cur->next);
    }

    /* there are more possible nodes for the <EncryptedKey> node */
    if(ctx->mode == xmlEncCtxModeEncryptedKey) {
	/* next is optional ReferenceList node (we simply ignore it) */
        if((cur != NULL) && (xmlSecCheckNodeName(cur, xmlSecNodeReferenceList, xmlSecEncNs))) {
	    cur = xmlSecGetNextElementNode(cur->next);
	}

        /* next is optional CarriedKeyName node (we simply ignore it) */
	if((cur != NULL) && (xmlSecCheckNodeName(cur, xmlSecNodeCarriedKeyName, xmlSecEncNs))) {
	    ctx->carriedKeyName = xmlNodeGetContent(cur);
	    if(ctx->carriedKeyName == NULL) {
		xmlSecError(XMLSEC_ERRORS_HERE,
			    NULL,
			    xmlSecErrorsSafeString(xmlSecNodeGetName(cur)),
			    XMLSEC_ERRORS_R_INVALID_NODE_CONTENT,
			    "node=%s",
			    xmlSecErrorsSafeString(xmlSecNodeCipherData));
		return(-1);
	    }
	    /* TODO: decode the name? */
	    cur = xmlSecGetNextElementNode(cur->next);
	}
    }

    /* if there is something left than it's an error */
    if(cur != NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    xmlSecErrorsSafeString(xmlSecNodeGetName(cur)),
		    NULL,
		    XMLSEC_ERRORS_R_UNEXPECTED_NODE,
		    XMLSEC_ERRORS_NO_MESSAGE);
	return(-1);
    }

    /* now read the encryption method node */
    if((ctx->encMethod == NULL) && (ctx->encMethodNode != NULL)) {
	ctx->encMethod = xmlSecTransformCtxNodeRead(&(ctx->encTransformCtx), ctx->encMethodNode,
						xmlSecTransformUsageEncryptionMethod);
	if(ctx->encMethod == NULL) {
	    xmlSecError(XMLSEC_ERRORS_HERE,
		    	NULL,
			"xmlSecTransformCtxNodeRead",
			XMLSEC_ERRORS_R_XMLSEC_FAILED,
			"node=%s",
			xmlSecErrorsSafeString(xmlSecNodeGetName(ctx->encMethodNode)));
	    return(-1);	    
	}	
    } else if(ctx->encMethod != NULL) {
	ret = xmlSecTransformCtxAppend(&(ctx->encTransformCtx), ctx->encMethod);
	if(ret < 0) {
    	    xmlSecError(XMLSEC_ERRORS_HERE,
			NULL,
			"xmlSecTransformCtxAppend",
			XMLSEC_ERRORS_R_XMLSEC_FAILED,
			XMLSEC_ERRORS_NO_MESSAGE);
	    return(-1);
	}
	ctx->dontDestroyEncMethod = 1;
    } else {
	/* TODO: add default global enc method? */
	xmlSecError(XMLSEC_ERRORS_HERE,
		    NULL,
		    NULL,
		    XMLSEC_ERRORS_R_INVALID_DATA,
		    "encryption method not specified");
	return(-1);
    }
    ctx->encMethod->encode = ctx->encrypt;
    
    /* we have encryption method, find key */
    ret = xmlSecTransformSetKeyReq(ctx->encMethod, &(ctx->keyInfoReadCtx.keyReq));
    if(ret < 0) {
    	xmlSecError(XMLSEC_ERRORS_HERE,
		    NULL,
		    "xmlSecTransformSetKeyReq",
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "transform=%s",
		    xmlSecErrorsSafeString(xmlSecTransformGetName(ctx->encMethod)));
	return(-1);
    }	
    	
    if((ctx->encKey == NULL) && 
       (ctx->keyInfoNode != NULL) && 
       (ctx->keyInfoReadCtx.keysMngr->getKey != NULL)) {
	
	ctx->encKey = (ctx->keyInfoReadCtx.keysMngr->getKey)(ctx->keyInfoNode, 
							     &(ctx->keyInfoReadCtx));
    }
    
    /* check that we have exactly what we want */
    if((ctx->encKey == NULL) || 
       (!xmlSecKeyMatch(ctx->encKey, NULL, &(ctx->keyInfoReadCtx.keyReq)))) {

	xmlSecError(XMLSEC_ERRORS_HERE,
		    NULL,
		    NULL,
		    XMLSEC_ERRORS_R_KEY_NOT_FOUND,
		    XMLSEC_ERRORS_NO_MESSAGE);
	return(-1);
    }
    
    /* set the key to the transform */
    ret = xmlSecTransformSetKey(ctx->encMethod, ctx->encKey);
    if(ret < 0) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    NULL,
		    "xmlSecTransformSetKey",
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "transform=%s",
		    xmlSecErrorsSafeString(xmlSecTransformGetName(ctx->encMethod)));
	return(-1);
    }

    /* if we need to write result to xml node then we need base64 encode it */
    if((ctx->encrypt) && (ctx->cipherValueNode != NULL)) {	
	xmlSecTransformPtr base64Encode;
	
	/* we need to add base64 encode transform */
	base64Encode = xmlSecTransformCtxCreateAndAppend(&(ctx->encTransformCtx), xmlSecTransformBase64Id);
    	if(base64Encode == NULL) {
    	    xmlSecError(XMLSEC_ERRORS_HERE,
			NULL,
			"xmlSecTransformCtxCreateAndAppend",
			XMLSEC_ERRORS_R_XMLSEC_FAILED,
			XMLSEC_ERRORS_NO_MESSAGE);
	    return(-1);
	}
	base64Encode->encode = 1;
	ctx->resultBase64Encoded = 1;
    }
    
    return(0);
}

static int 
xmlSecEncCtxCipherDataNodeWrite(xmlSecEncCtxPtr ctx) {
    int ret;
    
    xmlSecAssert2(ctx != NULL, -1);
    xmlSecAssert2(ctx->encResult != NULL, -1);
    xmlSecAssert2(ctx->encKey != NULL, -1);
    
    /* write encrypted data to xml (if requested) */
    if(ctx->cipherValueNode != NULL) {	
	xmlSecAssert2(xmlSecBufferGetData(ctx->encResult) != NULL, -1);

	xmlNodeSetContentLen(ctx->cipherValueNode,
			    xmlSecBufferGetData(ctx->encResult),
			    xmlSecBufferGetSize(ctx->encResult));
	ctx->replaced = 1;
    }

    /* update <dsig:KeyInfo/> node */
    if(ctx->keyInfoNode != NULL) {
	ret = xmlSecKeyInfoNodeWrite(ctx->keyInfoNode, ctx->encKey, &(ctx->keyInfoWriteCtx));
	if(ret < 0) {
    	    xmlSecError(XMLSEC_ERRORS_HERE,
			NULL,
			"xmlSecKeyInfoNodeWrite",
			XMLSEC_ERRORS_R_XMLSEC_FAILED,
			XMLSEC_ERRORS_NO_MESSAGE);
	    return(-1);
	}	
    }
    
    return(0);
}

static int 
xmlSecEncCtxCipherDataNodeRead(xmlSecEncCtxPtr ctx, xmlNodePtr node) {
    xmlNodePtr cur;
    int ret;
    
    xmlSecAssert2(ctx != NULL, -1);
    xmlSecAssert2(node != NULL, -1);
    
    cur = xmlSecGetNextElementNode(node->children);
    
    /* we either have CipherValue or CipherReference node  */
    xmlSecAssert2(ctx->cipherValueNode == NULL, -1);
    if((cur != NULL) && (xmlSecCheckNodeName(cur, xmlSecNodeCipherValue, xmlSecEncNs))) {
        /* don't need data from CipherData node when we are encrypting */
	if(ctx->encrypt == 0) {
	    xmlSecTransformPtr base64Decode;
	
	    /* we need to add base64 decode transform */
	    base64Decode = xmlSecTransformCtxCreateAndPrepend(&(ctx->encTransformCtx), xmlSecTransformBase64Id);
    	    if(base64Decode == NULL) {
    		xmlSecError(XMLSEC_ERRORS_HERE,
			    NULL,
			    "xmlSecTransformCtxCreateAndPrepend",
			    XMLSEC_ERRORS_R_XMLSEC_FAILED,
			    XMLSEC_ERRORS_NO_MESSAGE);
	        return(-1);
	    }
	}
	ctx->cipherValueNode = cur;
        cur = xmlSecGetNextElementNode(cur->next);
    } else if((cur != NULL) && (xmlSecCheckNodeName(cur, xmlSecNodeCipherReference, xmlSecEncNs))) {
        /* don't need data from CipherData node when we are encrypting */
	if(ctx->encrypt == 0) {
    	    ret = xmlSecEncCtxCipherReferenceNodeRead(ctx, cur);
	    if(ret < 0) {
		xmlSecError(XMLSEC_ERRORS_HERE,
		    	    NULL,
			    "xmlSecEncCtxCipherReferenceNodeRead",
			    XMLSEC_ERRORS_R_XMLSEC_FAILED,
			    "node=%s",
			    xmlSecErrorsSafeString(xmlSecNodeGetName(cur)));
		return(-1);	    
	    }
	}	
        cur = xmlSecGetNextElementNode(cur->next);
    }
    
    if(cur != NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    NULL,
		    xmlSecErrorsSafeString(xmlSecNodeGetName(cur)),
		    XMLSEC_ERRORS_R_INVALID_NODE,
		    XMLSEC_ERRORS_NO_MESSAGE);
	return(-1);
    }
    return(0);
}

static int 
xmlSecEncCtxCipherReferenceNodeRead(xmlSecEncCtxPtr ctx, xmlNodePtr node) {
    xmlNodePtr cur;
    xmlChar* uri;
    int ret;
    
    xmlSecAssert2(ctx != NULL, -1);
    xmlSecAssert2(node != NULL, -1);
    
    /* first read the optional uri attr */
    uri = xmlGetProp(node, xmlSecAttrURI);
    if(uri != NULL) {
	ret = xmlSecTransformCtxSetUri(&(ctx->encTransformCtx), uri, node);
	if(ret < 0) {
	    xmlSecError(XMLSEC_ERRORS_HERE,
		    	NULL,
			"xmlSecTransformCtxSetUri",
			XMLSEC_ERRORS_R_XMLSEC_FAILED,
			"uri=%s",
			xmlSecErrorsSafeString(uri));
	    xmlFree(uri);
	    return(-1);	    
	}		
	xmlFree(uri);
    }    
    cur = xmlSecGetNextElementNode(node->children);
    
    /* the only one node is optional Transforms node */
    if((cur != NULL) && (xmlSecCheckNodeName(cur, xmlSecNodeTransforms, xmlSecEncNs))) {
	ret = xmlSecTransformCtxNodesListRead(&(ctx->encTransformCtx), cur,
				    xmlSecTransformUsageDSigTransform);
	if(ret < 0) {
	    xmlSecError(XMLSEC_ERRORS_HERE,
		    	NULL,
			"xmlSecTransformCtxNodesListRead",
			XMLSEC_ERRORS_R_XMLSEC_FAILED,
			"node=%s",
			xmlSecErrorsSafeString(xmlSecNodeGetName(ctx->encMethodNode)));
	    return(-1);	    
	}	
        cur = xmlSecGetNextElementNode(cur->next);
    }
    
    /* if there is something left than it's an error */
    if(cur != NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    xmlSecErrorsSafeString(xmlSecNodeGetName(cur)),
		    NULL,
		    XMLSEC_ERRORS_R_UNEXPECTED_NODE,
		    XMLSEC_ERRORS_NO_MESSAGE);
	return(-1);
    }
    return(0);
}

void 
xmlSecEncCtxDebugDump(xmlSecEncCtxPtr ctx, FILE* output) {
    xmlSecAssert(ctx != NULL);

    switch(ctx->mode) {
	case xmlEncCtxModeEncryptedData:
	    if(ctx->encrypt) {    
		fprintf(output, "= DATA ENCRYPTION CONTEXT\n");
	    } else {
		fprintf(output, "= DATA DECRYPTION CONTEXT\n");
	    }
	    break;
	case xmlEncCtxModeEncryptedKey:
	    if(ctx->encrypt) {    
		fprintf(output, "= KEY ENCRYPTION CONTEXT\n");
	    } else {
		fprintf(output, "= KEY DECRYPTION CONTEXT\n");
	    }
	    break;
    }
    fprintf(output, "== Status: %s\n",
	    (ctx->replaced) ? "replaced" : "not-replaced" );
    if(ctx->id != NULL) {
	fprintf(output, "== Id: \"%s\"\n", ctx->id);
    }
    if(ctx->type != NULL) {
	fprintf(output, "== Type: \"%s\"\n", ctx->type);
    }
    if(ctx->mimeType != NULL) {
	fprintf(output, "== MimeType: \"%s\"\n", ctx->mimeType);
    }
    if(ctx->encoding != NULL) {
	fprintf(output, "== Encoding: \"%s\"\n", ctx->encoding);
    }
    if(ctx->recipient != NULL) {
	fprintf(output, "== Recipient: \"%s\"\n", ctx->recipient);
    }
    if(ctx->carriedKeyName != NULL) {
	fprintf(output, "== CarriedKeyName: \"%s\"\n", ctx->carriedKeyName);
    }
    
    fprintf(output, "== Key Info Read Ctx:\n");
    xmlSecKeyInfoCtxDebugDump(&(ctx->keyInfoReadCtx), output);
    fprintf(output, "== Key Info Write Ctx:\n");
    xmlSecKeyInfoCtxDebugDump(&(ctx->keyInfoWriteCtx), output);

    xmlSecTransformCtxDebugDump(&(ctx->encTransformCtx), output);
    
    if((ctx->encResult != NULL) && 
       (xmlSecBufferGetData(ctx->encResult) != NULL) && 
       (ctx->resultBase64Encoded != 0)) {

	fprintf(output, "== Result - start buffer:\n");
	fwrite(xmlSecBufferGetData(ctx->encResult), 
	       xmlSecBufferGetSize(ctx->encResult), 1,
	       output);
	fprintf(output, "\n== Result - end buffer\n");
    } else {
	fprintf(output, "== Result: %d bytes\n",
		xmlSecBufferGetSize(ctx->encResult));
    }
}

void 
xmlSecEncCtxDebugXmlDump(xmlSecEncCtxPtr ctx, FILE* output) {
    xmlSecAssert(ctx != NULL);

    if(ctx->encrypt) {    
        fprintf(output, "<EncryptionContext>\n");
    } else {
        fprintf(output, "<DecryptionContext type=\"%s\">\n",
	    (ctx->replaced) ? "replaced" : "not-replaced" );
    }

    switch(ctx->mode) {
	case xmlEncCtxModeEncryptedData:
	    if(ctx->encrypt) {    
		fprintf(output, "<DataEncryptionContext ");
	    } else {
		fprintf(output, "<DataDecryptionContext ");
	    }
	    break;
	case xmlEncCtxModeEncryptedKey:
	    if(ctx->encrypt) {    
		fprintf(output, "<KeyEncryptionContext ");
	    } else {
		fprintf(output, "<KeyDecryptionContext ");
	    }
	    break;
    }
    fprintf(output, "status=\"%s\" >\n", (ctx->replaced) ? "replaced" : "not-replaced" );

    if(ctx->id != NULL) {
	fprintf(output, "<Id>%s</Id>\n", ctx->id);
    }
    if(ctx->type != NULL) {
	fprintf(output, "<Type>%s</Type>\n", ctx->type);
    }
    if(ctx->mimeType != NULL) {
	fprintf(output, "<MimeType>%s</MimeType>\n", ctx->mimeType);
    }
    if(ctx->encoding != NULL) {
	fprintf(output, "<Encoding>%s</Encoding>\n", ctx->encoding);
    }
    if(ctx->recipient != NULL) {
	fprintf(output, "<Recipient>%s</Recipient>\n", ctx->recipient);
    }
    if(ctx->carriedKeyName != NULL) {
	fprintf(output, "<CarriedKeyName>%s</CarriedKeyName>\n", ctx->carriedKeyName);
    }

    fprintf(output, "<KeyInfoReadCtx>\n");
    xmlSecKeyInfoCtxDebugXmlDump(&(ctx->keyInfoReadCtx), output);
    fprintf(output, "</KeyInfoReadCtx>\n");

    fprintf(output, "<KeyInfoWriteCtx>\n");
    xmlSecKeyInfoCtxDebugXmlDump(&(ctx->keyInfoWriteCtx), output);
    fprintf(output, "</KeyInfoWriteCtx>\n");
    xmlSecTransformCtxDebugXmlDump(&(ctx->encTransformCtx), output);

    if((ctx->encResult != NULL) && 
       (xmlSecBufferGetData(ctx->encResult) != NULL) && 
       (ctx->resultBase64Encoded != 0)) {

	fprintf(output, "<Result>");
	fwrite(xmlSecBufferGetData(ctx->encResult), 
	       xmlSecBufferGetSize(ctx->encResult), 1,
	       output);
	fprintf(output, "</Result>\n");
    } else {
	fprintf(output, "<Result size=\"%d\" />\n",
	       xmlSecBufferGetSize(ctx->encResult));
    }

    switch(ctx->mode) {
	case xmlEncCtxModeEncryptedData:
	    if(ctx->encrypt) {    
		fprintf(output, "</DataEncryptionContext>\n");
	    } else {
		fprintf(output, "</DataDecryptionContext>\n");
	    }
	    break;
	case xmlEncCtxModeEncryptedKey:
	    if(ctx->encrypt) {    
		fprintf(output, "</KeyEncryptionContext>\n");
	    } else {
		fprintf(output, "</KeyDecryptionContext>\n");
	    }
	    break;
    }
}

#endif /* XMLSEC_NO_XMLENC */

