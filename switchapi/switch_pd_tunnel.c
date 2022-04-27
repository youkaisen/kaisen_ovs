/*
Copyright 2013-present Barefoot Networks, Inc.
Copyright(c) 2021 Intel Corporation.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include <openvswitch/vlog.h>
#include "switch_tunnel.h"
#include "switch_internal.h"
#include <config.h>

#include <bf_types/bf_types.h>
#include <port_mgr/dpdk/bf_dpdk_port_if.h>
#include "bf_rt/bf_rt_common.h"
#include "bf_rt/bf_rt_session.h"
#include "bf_rt/bf_rt_init.h"
#include "bf_rt/bf_rt_info.h"
#include "bf_rt/bf_rt_table.h"
#include "bf_rt/bf_rt_table_key.h"
#include "bf_rt/bf_rt_table_data.h"
#include "switch_pd_utils.h"

VLOG_DEFINE_THIS_MODULE(switch_pd_tunnel);

switch_status_t switch_pd_tunnel_entry(
    switch_device_t device,
    const switch_api_tunnel_info_t *api_tunnel_info_t,
    bool entry_add) {

    bf_status_t status;

    bf_rt_id_t field_id;
    bf_rt_id_t action_id;
    bf_rt_id_t data_field_id;

    bf_rt_target_t dev_tgt;
    bf_rt_session_hdl *session;
    bf_rt_info_hdl *bfrt_info_hdl;
    bf_rt_table_key_hdl *key_hdl;
    bf_rt_table_data_hdl *data_hdl;
    const bf_rt_table_hdl *table_hdl;
    uint32_t network_byte_order;

    dev_tgt.dev_id = device;
    dev_tgt.pipe_id = 0;
    dev_tgt.direction = 0xFF;
    dev_tgt.prsr_id = 0xFF;

    status = switch_pd_allocate_handle_session(device, PROGRAM_NAME,
                                               &bfrt_info_hdl, &session);
    if(status != BF_SUCCESS) {
        VLOG_ERR("Switch PD handle fail");
        return switch_pd_status_to_status(status);
    }

    table_hdl = (bf_rt_table_hdl *)malloc(sizeof(table_hdl));
    status = bf_rt_table_from_name_get(bfrt_info_hdl, "vxlan_encap_mod_table",
                                       &table_hdl);
    if(status != BF_SUCCESS) {
        VLOG_ERR("Unable to get table handle for vxlan_encap_mod_table");
        goto dealloc_handle_session;
    }

    status = bf_rt_table_key_allocate(table_hdl, &key_hdl);
    if(status != BF_SUCCESS) {
        VLOG_ERR("Unable to get key handle");
        goto dealloc_handle_session;
    }

    field_id = 1; // vendormeta_mod_data_ptr which is tunnel ID
    status = bf_rt_key_field_set_value(key_hdl, field_id, 0 /*vni value*/);
    if(status != BF_SUCCESS) {
        VLOG_ERR("Unable to set value for key ID: %d", field_id);
        goto dealloc_handle_session;
    }

    if (entry_add) {
        /* Add an entry to target */
        action_id = 20733968; // Action is vxlan_encap
        status = bf_rt_table_action_data_allocate(table_hdl, action_id,
                                                  &data_hdl);
        if(status != BF_SUCCESS) {
            VLOG_ERR("Unable to get action allocator for ID : %d", action_id);
            goto dealloc_handle_session;
        }

        data_field_id = 1; // Action value src_addr

        network_byte_order = ntohl(api_tunnel_info_t->src_ip.ip.v4addr);
        status = bf_rt_data_field_set_value_ptr(data_hdl, data_field_id,
                                            (const uint8_t *)&network_byte_order,
                                            sizeof(uint32_t));
        if(status != BF_SUCCESS) {
            VLOG_ERR("Unable to set action value for ID: %d", data_field_id);
            goto dealloc_handle_session;
        }

        data_field_id = 2; // Action value dst_addr

        network_byte_order = ntohl(api_tunnel_info_t->dst_ip.ip.v4addr);
        status = bf_rt_data_field_set_value_ptr(data_hdl, data_field_id,
                                            (const uint8_t *)&network_byte_order,
                                            sizeof(uint32_t));
        if(status != BF_SUCCESS) {
            VLOG_ERR("Unable to set action value for ID: %d", data_field_id);
            goto dealloc_handle_session;
        }

        data_field_id = 3; // Action value dst_port

        uint16_t network_byte_order_udp = ntohs(api_tunnel_info_t->udp_port);
        status = bf_rt_data_field_set_value_ptr(data_hdl, data_field_id,
                                            (const uint8_t *)&network_byte_order_udp,
                                            sizeof(uint16_t));
        if(status != BF_SUCCESS) {
            VLOG_ERR("Unable to set action value for ID: %d", data_field_id);
            goto dealloc_handle_session;
        }

        data_field_id = 4; // // Action value vni
        status = bf_rt_data_field_set_value_ptr(data_hdl, data_field_id, 0,
                                            sizeof(uint32_t));
        if(status != BF_SUCCESS) {
            VLOG_ERR("Unable to set action value for ID: %d", data_field_id);
            goto dealloc_handle_session;
        }

        status = bf_rt_table_entry_add(table_hdl, session, &dev_tgt, key_hdl,
                                       data_hdl);
        if(status != BF_SUCCESS) {
            VLOG_ERR("Unable to add table entry");
            goto dealloc_handle_session;
        }
    } else {
        /* Delete an entry from target */
        status = bf_rt_table_entry_del(table_hdl, session, &dev_tgt, key_hdl);
        if(status != BF_SUCCESS) {
            VLOG_ERR("Unable to delete table entry");
            goto dealloc_handle_session;
        }
    }

dealloc_handle_session:
    status = switch_pd_deallocate_handle_session(key_hdl, data_hdl, session,
                                                 entry_add);
    if(status != BF_SUCCESS) {
        VLOG_ERR("Unable to deallocate session and handles");
        return switch_pd_status_to_status(status);
    }

    return switch_pd_status_to_status(status);
}

switch_status_t switch_pd_tunnel_term_entry(
    switch_device_t device,
    const switch_api_tunnel_term_info_t *api_tunnel_term_info_t,
    bool entry_add) {

    bf_status_t status;

    bf_rt_id_t field_id;
    bf_rt_id_t action_id;
    bf_rt_id_t data_field_id;

    bf_rt_target_t dev_tgt;
    bf_rt_session_hdl *session;
    bf_rt_info_hdl *bfrt_info_hdl;
    bf_rt_table_key_hdl *key_hdl;
    bf_rt_table_data_hdl *data_hdl;
    const bf_rt_table_hdl *table_hdl;
    uint32_t network_byte_order;

    dev_tgt.dev_id = device;
    dev_tgt.pipe_id = 0;
    dev_tgt.direction = 0xFF;
    dev_tgt.prsr_id = 0xFF;

    status = switch_pd_allocate_handle_session(device, PROGRAM_NAME,
                                               &bfrt_info_hdl, &session);
    if(status != BF_SUCCESS) {
        VLOG_ERR("Switch PD handle fail");
        return switch_pd_status_to_status(status);
    }

    table_hdl = (bf_rt_table_hdl *)malloc(sizeof(table_hdl));
    status = bf_rt_table_from_name_get(bfrt_info_hdl, "ipv4_tunnel_term_table",
                                       &table_hdl);
    if(status != BF_SUCCESS) {
        VLOG_ERR("Unable to get table handle for ipv4_tunnel_term_table");
        goto dealloc_handle_session;
    }

    status = bf_rt_table_key_allocate(table_hdl, &key_hdl);
    if(status != BF_SUCCESS) {
        VLOG_ERR("Unable to get key handle");
        goto dealloc_handle_session;
    }

    field_id = 1; // Match key local_metadata.tunnel.tun_type
    status = bf_rt_key_field_set_value(key_hdl, field_id, 0);
    if(status != BF_SUCCESS) {
        VLOG_ERR("Unable to set value for key ID: %d", field_id);
        goto dealloc_handle_session;
    }

    field_id = 2; // Match key ipv4_src

    network_byte_order = ntohl(api_tunnel_term_info_t->src_ip.ip.v4addr);
    status = bf_rt_key_field_set_value_ptr(key_hdl, field_id,
                                           (const uint8_t *)&network_byte_order,
                                           sizeof(uint32_t));
    if(status != BF_SUCCESS) {
        VLOG_ERR("Unable to set value for key ID: %d", field_id);
        goto dealloc_handle_session;
    }

    field_id = 3; // Match key ipv4_dst

    network_byte_order = ntohl(api_tunnel_term_info_t->dst_ip.ip.v4addr);
    status = bf_rt_key_field_set_value_ptr(key_hdl, field_id,
                                           (const uint8_t *)&network_byte_order,
                                           sizeof(uint32_t));
    if(status != BF_SUCCESS) {
        VLOG_ERR("Unable to set value for key ID: %d", field_id);
        goto dealloc_handle_session;
    }

    if (entry_add) {
        /* Add an entry to target */
        action_id = 32579284; // Action is decap_outer_ipv4
        status = bf_rt_table_action_data_allocate(table_hdl, action_id,
                                                  &data_hdl);
        if(status != BF_SUCCESS) {
            VLOG_ERR("Unable to get action allocator for ID : %d", action_id);
            goto dealloc_handle_session;
        }

        data_field_id = 1; // Action value tunnel_id
        status = bf_rt_data_field_set_value(data_hdl, data_field_id,
                                            api_tunnel_term_info_t->tunnel_id);
        if(status != BF_SUCCESS) {
            VLOG_ERR("Unable to set action value for ID: %d", data_field_id);
            goto dealloc_handle_session;
        }

        status = bf_rt_table_entry_add(table_hdl, session, &dev_tgt, key_hdl,
                                       data_hdl);
        if(status != BF_SUCCESS) {
            VLOG_ERR("Unable to add table entry");
            goto dealloc_handle_session;
        }
    } else {
        /* Delete an entry from target */
        status = bf_rt_table_entry_del(table_hdl, session, &dev_tgt, key_hdl);
        if(status != BF_SUCCESS) {
            VLOG_ERR("Unable to delete table entry");
            goto dealloc_handle_session;
        }
    }

dealloc_handle_session:
    status = switch_pd_deallocate_handle_session(key_hdl, data_hdl, session,
                                                 entry_add);
    if(status != BF_SUCCESS) {
        VLOG_ERR("Unable to deallocate session and handles");
        return switch_pd_status_to_status(status);
    }

    return switch_pd_status_to_status(status);
}
