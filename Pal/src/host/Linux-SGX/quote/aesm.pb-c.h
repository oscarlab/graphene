/* Generated by the protocol buffer compiler.  DO NOT EDIT! */
/* Generated from: aesm.proto */

#ifndef PROTOBUF_C_aesm_2eproto__INCLUDED
#define PROTOBUF_C_aesm_2eproto__INCLUDED

#include <protobuf-c/protobuf-c.h>

PROTOBUF_C__BEGIN_DECLS

#if PROTOBUF_C_VERSION_NUMBER < 1000000
# error This file was generated by a newer version of protoc-c which is incompatible with your libprotobuf-c headers. Please update your headers.
#elif 1002001 < PROTOBUF_C_MIN_COMPILER_VERSION
# error This file was generated by an older version of protoc-c which is incompatible with your libprotobuf-c headers. Please regenerate this file with a newer version of protoc-c.
#endif


typedef struct _Request Request;
typedef struct _Request__InitQuoteRequest Request__InitQuoteRequest;
typedef struct _Request__GetQuoteRequest Request__GetQuoteRequest;
typedef struct _Response Response;
typedef struct _Response__InitQuoteResponse Response__InitQuoteResponse;
typedef struct _Response__GetQuoteResponse Response__GetQuoteResponse;


/* --- enums --- */


/* --- messages --- */

struct  _Request__InitQuoteRequest
{
  ProtobufCMessage base;
  protobuf_c_boolean has_timeout;
  uint32_t timeout;
};
#define REQUEST__INIT_QUOTE_REQUEST__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&request__init_quote_request__descriptor) \
    , 0,0 }


struct  _Request__GetQuoteRequest
{
  ProtobufCMessage base;
  ProtobufCBinaryData report;
  uint32_t quote_type;
  ProtobufCBinaryData spid;
  protobuf_c_boolean has_nonce;
  ProtobufCBinaryData nonce;
  protobuf_c_boolean has_sig_rl;
  ProtobufCBinaryData sig_rl;
  uint32_t buf_size;
  protobuf_c_boolean has_qe_report;
  protobuf_c_boolean qe_report;
  protobuf_c_boolean has_timeout;
  uint32_t timeout;
};
#define REQUEST__GET_QUOTE_REQUEST__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&request__get_quote_request__descriptor) \
    , {0,NULL}, 0, {0,NULL}, 0,{0,NULL}, 0,{0,NULL}, 0, 0,0, 0,0 }


struct  _Request
{
  ProtobufCMessage base;
  Request__InitQuoteRequest *initquotereq;
  Request__GetQuoteRequest *getquotereq;
};
#define REQUEST__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&request__descriptor) \
    , NULL, NULL }


struct  _Response__InitQuoteResponse
{
  ProtobufCMessage base;
  uint32_t errorcode;
  protobuf_c_boolean has_targetinfo;
  ProtobufCBinaryData targetinfo;
  protobuf_c_boolean has_gid;
  ProtobufCBinaryData gid;
};
#define RESPONSE__INIT_QUOTE_RESPONSE__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&response__init_quote_response__descriptor) \
    , 1u, 0,{0,NULL}, 0,{0,NULL} }


struct  _Response__GetQuoteResponse
{
  ProtobufCMessage base;
  uint32_t errorcode;
  protobuf_c_boolean has_quote;
  ProtobufCBinaryData quote;
  protobuf_c_boolean has_qe_report;
  ProtobufCBinaryData qe_report;
};
#define RESPONSE__GET_QUOTE_RESPONSE__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&response__get_quote_response__descriptor) \
    , 1u, 0,{0,NULL}, 0,{0,NULL} }


struct  _Response
{
  ProtobufCMessage base;
  Response__InitQuoteResponse *initquoteres;
  Response__GetQuoteResponse *getquoteres;
};
#define RESPONSE__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&response__descriptor) \
    , NULL, NULL }


/* Request__InitQuoteRequest methods */
void   request__init_quote_request__init
                     (Request__InitQuoteRequest         *message);
/* Request__GetQuoteRequest methods */
void   request__get_quote_request__init
                     (Request__GetQuoteRequest         *message);
/* Request methods */
void   request__init
                     (Request         *message);
size_t request__get_packed_size
                     (const Request   *message);
size_t request__pack
                     (const Request   *message,
                      uint8_t             *out);
size_t request__pack_to_buffer
                     (const Request   *message,
                      ProtobufCBuffer     *buffer);
Request *
       request__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   request__free_unpacked
                     (Request *message,
                      ProtobufCAllocator *allocator);
/* Response__InitQuoteResponse methods */
void   response__init_quote_response__init
                     (Response__InitQuoteResponse         *message);
/* Response__GetQuoteResponse methods */
void   response__get_quote_response__init
                     (Response__GetQuoteResponse         *message);
/* Response methods */
void   response__init
                     (Response         *message);
size_t response__get_packed_size
                     (const Response   *message);
size_t response__pack
                     (const Response   *message,
                      uint8_t             *out);
size_t response__pack_to_buffer
                     (const Response   *message,
                      ProtobufCBuffer     *buffer);
Response *
       response__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   response__free_unpacked
                     (Response *message,
                      ProtobufCAllocator *allocator);
/* --- per-message closures --- */

typedef void (*Request__InitQuoteRequest_Closure)
                 (const Request__InitQuoteRequest *message,
                  void *closure_data);
typedef void (*Request__GetQuoteRequest_Closure)
                 (const Request__GetQuoteRequest *message,
                  void *closure_data);
typedef void (*Request_Closure)
                 (const Request *message,
                  void *closure_data);
typedef void (*Response__InitQuoteResponse_Closure)
                 (const Response__InitQuoteResponse *message,
                  void *closure_data);
typedef void (*Response__GetQuoteResponse_Closure)
                 (const Response__GetQuoteResponse *message,
                  void *closure_data);
typedef void (*Response_Closure)
                 (const Response *message,
                  void *closure_data);

/* --- services --- */


/* --- descriptors --- */

extern const ProtobufCMessageDescriptor request__descriptor;
extern const ProtobufCMessageDescriptor request__init_quote_request__descriptor;
extern const ProtobufCMessageDescriptor request__get_quote_request__descriptor;
extern const ProtobufCMessageDescriptor response__descriptor;
extern const ProtobufCMessageDescriptor response__init_quote_response__descriptor;
extern const ProtobufCMessageDescriptor response__get_quote_response__descriptor;

PROTOBUF_C__END_DECLS


#endif  /* PROTOBUF_C_aesm_2eproto__INCLUDED */
