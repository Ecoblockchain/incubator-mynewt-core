/*
// Copyright (c) 2016 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/

#ifndef OC_BUFFER_H
#define OC_BUFFER_H

#ifdef __cplusplus
extern "C" {
#endif

struct oc_message_s;
struct oc_message_s *oc_allocate_message(void);
void oc_message_add_ref(struct oc_message_s *message);
void oc_message_unref(struct oc_message_s *message);

void oc_recv_message(struct oc_message_s *message);
void oc_send_message(struct oc_message_s *message);

#ifdef __cplusplus
}
#endif

#endif /* OC_BUFFER_H */
