#ifndef SRC_VISITOR_H_
#define SRC_VISITOR_H_

#include "common.h"
#include "error.h"
#include "queue.h"

/* Forward declarations */
struct cd_state_s;

typedef struct cd_node_s cd_node_t;
typedef struct cd_edge_s cd_edge_t;
typedef enum cd_node_type_e cd_node_type_t;
typedef enum cd_edge_type_e cd_edge_type_t;

enum cd_node_type_e {
  kCDNodeHidden,
  kCDNodeArray,
  kCDNodeString,
  kCDNodeObject,
  kCDNodeCode,
  kCDNodeClosure,
  kCDNodeRegExp,
  kCDNodeNumber,
  kCDNodeNative,
  kCDNodeSynthetic,
  kCDNodeConString,
  kCDNodeSlicedString
};

enum cd_edge_type_e {
  kCDEdgeContext,
  kCDEdgeElement,
  kCDEdgeProperty,
  kCDEdgeInternal,
  kCDEdgeHidden,
  kCDEdgeShortcut,
  kCDEdgeWeak
};

struct cd_node_s {
  QUEUE member;

  /* Raw V8 stuff right from the Heap */
  void* obj;
  void* map;
  int v8_type;

  cd_node_type_t type;
  int id;
  int name;
  int size;

  struct {
    QUEUE incoming;
    QUEUE outgoing;
    int outgoing_count;
  } edges;
};

struct cd_edge_s {
  QUEUE in;
  QUEUE out;

  cd_edge_type_t type;
  struct {
    cd_node_t* from;
    cd_node_t* to;
  } key;
  int name;
};

cd_error_t cd_visitor_init(struct cd_state_s* state);
void cd_visitor_destroy(struct cd_state_s* state);

cd_error_t cd_visit_roots(struct cd_state_s* state);

cd_error_t cd_queue_ptr(struct cd_state_s* state,
                        cd_node_t* from,
                        void* ptr,
                        void* map,
                        cd_edge_type_t type,
                        int name,
                        int tag,
                        cd_node_t** out);

#endif  /* SRC_VISITOR_H_ */
