# L4_rule_chain_dsl Normative Specification

Layer: `rule_chain_dsl`

This normative document defines the machine-checkable contract for `rule_chain_dsl`.

## Contract Block

```json
{
  "contract_block_version": 1,
  "contract_semantics_version": 1,
  "layer_id": "rule_chain_dsl",
  "layer_version": 1,
  "schema_scope": {
    "owned_pointers": [
      "/chains",
      "/signals",
      "/gates",
      "/validation",
      "/execution/backend/sierra_chart/layout_contract/readiness"
    ],
    "owned_defs": [],
    "notes": "Chains, gates and signals DSL compilation surface."
  },
  "dependencies": {
    "allowed_layer_ids": [
      "core_identity_io",
      "universe_data",
      "studies_features"
    ],
    "allowed_interfaces": [
      {
        "interface_id": "plan_access",
        "version": 1
      },
      {
        "interface_id": "diagnostics",
        "version": 1
      },
      {
        "interface_id": "hashing",
        "version": 1
      }
    ]
  },
  "ast_nodes": [
    {
      "node_id": "RuleChainDslNode",
      "kind": "struct",
      "fields": [
        {
          "name": "layer_id",
          "type": "string",
          "required": true,
          "default": "rule_chain_dsl"
        },
        {
          "name": "scope",
          "type": "json_pointer[]",
          "required": true
        }
      ],
      "invariants": [
        {
          "id": "INV_POINTER_SCOPE_ENFORCED",
          "severity": "error",
          "rule": {
            "kind": "no_cross_layer_pointer_access",
            "params": {
              "owned_pointers": [
                "/chains",
                "/signals",
                "/gates",
                "/validation",
                "/execution/backend/sierra_chart/layout_contract/readiness"
              ]
            }
          }
        }
      ]
    }
  ],
  "canonicalization_rules": {
    "cnf_version": 1,
    "defaults_policy": "apply_schema_defaults_then_contract_defaults",
    "ordering_policy": "stable_lexicographic",
    "ref_resolution_policy": {
      "must_resolve": true,
      "on_unresolved": "error"
    }
  },
  "public_api": {
    "functions": [
      {
        "id": "FN_VALIDATE_LAYER",
        "name": "ValidateRuleChainDsl",
        "signature": {
          "namespace": "sre::layers",
          "return_type": "Status",
          "params": [
            {
              "name": "plan_view",
              "type": "const ScopedPlanView&"
            },
            {
              "name": "diag",
              "type": "DiagnosticSink&"
            }
          ],
          "noexcept": true
        },
        "stability": "stable"
      },
      {
        "id": "FN_REGISTER_LAYER_API",
        "name": "RegisterRuleChainDslApi",
        "signature": {
          "namespace": "sre::layers",
          "return_type": "void",
          "params": [],
          "noexcept": true
        },
        "stability": "stable"
      }
    ],
    "types": [
      {
        "id": "TY_LAYER_CONTEXT",
        "name": "LayerContext",
        "kind": "struct"
      },
      {
        "id": "TY_LAYER_RESULT",
        "name": "LayerResult",
        "kind": "struct"
      }
    ],
    "error_codes": [
      {
        "code": "E_LAYER_CHAIN_DSL_INVALID",
        "layer_id": "rule_chain_dsl",
        "severity": "error",
        "message_template": "rule_chain_dsl contract violation: {reason}"
      },
      {
        "code": "E_LAYER_AST_CHECK_FAILED",
        "layer_id": "rule_chain_dsl",
        "severity": "error",
        "message_template": "Normative AST check failed: {check_id}"
      }
    ]
  },
  "normative_ast_checks": {
    "cnf_basis": "cnf_only",
    "check_groups": [
      {
        "group_id": "CHKGRP_API_SIGNATURES",
        "kind": "api_signatures",
        "checks": [
          {
            "id": "CHK_API_VALIDATE_SIGNATURE",
            "severity": "error",
            "assert": {
              "kind": "function_exists_with_signature",
              "params": {
                "function": "ValidateRuleChainDsl",
                "namespace": "sre::layers",
                "return_type": "Status"
              }
            }
          }
        ]
      },
      {
        "group_id": "CHKGRP_FILE_LAYOUT",
        "kind": "file_layout",
        "checks": [
          {
            "id": "CHK_FILE_NORMATIVE_DOC_EXISTS",
            "severity": "error",
            "assert": {
              "kind": "file_exists",
              "params": {
                "path": "docs/normative/L4_rule_chain_dsl.md"
              }
            }
          },
          {
            "id": "CHK_FILE_CONTRACT_MANIFEST_EXISTS",
            "severity": "error",
            "assert": {
              "kind": "file_exists",
              "params": {
                "path": "contracts/L4_rule_chain_dsl.manifest.json"
              }
            }
          },
          {
            "id": "CHK_FILE_FORBIDDEN_TEMP",
            "severity": "warn",
            "assert": {
              "kind": "file_forbidden",
              "params": {
                "path": "src/layers/L4_rule_chain_dsl/tmp.txt"
              }
            }
          }
        ]
      },
      {
        "group_id": "CHKGRP_MODULE_BOUNDARIES",
        "kind": "module_boundaries",
        "checks": [
          {
            "id": "CHK_BOUNDARY_ONLY_ALLOWED_DEPS",
            "severity": "error",
            "assert": {
              "kind": "only_allowed_layer_deps",
              "params": {
                "layer_id": "rule_chain_dsl",
                "allowed_layer_ids": [
                  "core_identity_io",
                  "universe_data",
                  "studies_features"
                ]
              }
            }
          }
        ]
      },
      {
        "group_id": "CHKGRP_PLAN_AST_SHAPE",
        "kind": "plan_ast_shape",
        "checks": [
          {
            "id": "CHK_AST_LAYER_NODE_PRESENT",
            "severity": "error",
            "assert": {
              "kind": "cnf_has_node",
              "params": {
                "node_id": "RuleChainDslNode"
              }
            }
          },
          {
            "id": "CHK_AST_POINTER_SCOPE",
            "severity": "error",
            "assert": {
              "kind": "cnf_pointer_in_scope",
              "params": {
                "pointers": [
                  "/chains",
                  "/signals",
                  "/gates",
                  "/validation",
                  "/execution/backend/sierra_chart/layout_contract/readiness"
                ]
              }
            }
          }
        ]
      },
      {
        "group_id": "CHKGRP_DIAGNOSTICS",
        "kind": "diagnostics",
        "checks": [
          {
            "id": "CHK_DIAG_INVALID_01",
            "severity": "error",
            "assert": {
              "kind": "invalid_fixture_yields_error",
              "params": {
                "fixture_id": "FIX_INVALID_01",
                "expected_code": "E_LAYER_CHAIN_DSL_INVALID",
                "expected_blame_pointer": "/chains"
              }
            }
          },
          {
            "id": "CHK_DIAG_INVALID_02",
            "severity": "error",
            "assert": {
              "kind": "invalid_fixture_yields_error",
              "params": {
                "fixture_id": "FIX_INVALID_02",
                "expected_code": "E_LAYER_CHAIN_DSL_INVALID",
                "expected_blame_pointer": "/chains"
              }
            }
          },
          {
            "id": "CHK_DIAG_INVALID_03",
            "severity": "error",
            "assert": {
              "kind": "invalid_fixture_yields_error",
              "params": {
                "fixture_id": "FIX_INVALID_03",
                "expected_code": "E_LAYER_CHAIN_DSL_INVALID",
                "expected_blame_pointer": "/chains"
              }
            }
          },
          {
            "id": "CHK_DIAG_INVALID_04",
            "severity": "error",
            "assert": {
              "kind": "invalid_fixture_yields_error",
              "params": {
                "fixture_id": "FIX_INVALID_04",
                "expected_code": "E_LAYER_CHAIN_DSL_INVALID",
                "expected_blame_pointer": "/chains"
              }
            }
          },
          {
            "id": "CHK_DIAG_INVALID_05",
            "severity": "error",
            "assert": {
              "kind": "invalid_fixture_yields_error",
              "params": {
                "fixture_id": "FIX_INVALID_05",
                "expected_code": "E_LAYER_CHAIN_DSL_INVALID",
                "expected_blame_pointer": "/chains"
              }
            }
          },
          {
            "id": "CHK_DIAG_INVALID_06",
            "severity": "error",
            "assert": {
              "kind": "invalid_fixture_yields_error",
              "params": {
                "fixture_id": "FIX_INVALID_06",
                "expected_code": "E_LAYER_CHAIN_DSL_INVALID",
                "expected_blame_pointer": "/chains"
              }
            }
          },
          {
            "id": "CHK_DIAG_INVALID_07",
            "severity": "error",
            "assert": {
              "kind": "invalid_fixture_yields_error",
              "params": {
                "fixture_id": "FIX_INVALID_07",
                "expected_code": "E_LAYER_CHAIN_DSL_INVALID",
                "expected_blame_pointer": "/chains"
              }
            }
          },
          {
            "id": "CHK_DIAG_INVALID_08",
            "severity": "error",
            "assert": {
              "kind": "invalid_fixture_yields_error",
              "params": {
                "fixture_id": "FIX_INVALID_08",
                "expected_code": "E_LAYER_CHAIN_DSL_INVALID",
                "expected_blame_pointer": "/chains"
              }
            }
          },
          {
            "id": "CHK_DIAG_INVALID_09",
            "severity": "error",
            "assert": {
              "kind": "invalid_fixture_yields_error",
              "params": {
                "fixture_id": "FIX_INVALID_09",
                "expected_code": "E_LAYER_CHAIN_DSL_INVALID",
                "expected_blame_pointer": "/chains"
              }
            }
          },
          {
            "id": "CHK_DIAG_INVALID_10",
            "severity": "error",
            "assert": {
              "kind": "invalid_fixture_yields_error",
              "params": {
                "fixture_id": "FIX_INVALID_10",
                "expected_code": "E_LAYER_CHAIN_DSL_INVALID",
                "expected_blame_pointer": "/chains"
              }
            }
          }
        ]
      }
    ]
  },
  "compile_outputs": {
    "emits": [
      "execution_dag",
      "lineage_dag",
      "diagnostics",
      "manifest"
    ],
    "artifacts": [
      {
        "id": "ART_LAYER_API_HEADER",
        "path": "generated/contracts/L4_rule_chain_dsl_api.h",
        "kind": "header"
      },
      {
        "id": "ART_LAYER_CONTRACT",
        "path": "contracts/L4_rule_chain_dsl.manifest.json",
        "kind": "manifest"
      },
      {
        "id": "ART_LAYER_TEST_STRUCTURAL",
        "path": "generated/tests/L4_rule_chain_dsl_structural.json",
        "kind": "tests"
      },
      {
        "id": "ART_LAYER_TEST_SEMANTIC",
        "path": "generated/tests/L4_rule_chain_dsl_semantic.json",
        "kind": "tests"
      },
      {
        "id": "ART_LAYER_DOC",
        "path": "docs/normative/L4_rule_chain_dsl.md",
        "kind": "doc"
      }
    ]
  },
  "fixtures": {
    "valid": [
      {
        "id": "FIX_VALID_01",
        "path": "fixtures/L4_rule_chain_dsl/valid/fix_valid_01.json",
        "expect": {}
      },
      {
        "id": "FIX_VALID_02",
        "path": "fixtures/L4_rule_chain_dsl/valid/fix_valid_02.json",
        "expect": {}
      },
      {
        "id": "FIX_VALID_03",
        "path": "fixtures/L4_rule_chain_dsl/valid/fix_valid_03.json",
        "expect": {}
      },
      {
        "id": "FIX_VALID_04",
        "path": "fixtures/L4_rule_chain_dsl/valid/fix_valid_04.json",
        "expect": {}
      },
      {
        "id": "FIX_VALID_05",
        "path": "fixtures/L4_rule_chain_dsl/valid/fix_valid_05.json",
        "expect": {}
      },
      {
        "id": "FIX_VALID_06",
        "path": "fixtures/L4_rule_chain_dsl/valid/fix_valid_06.json",
        "expect": {}
      },
      {
        "id": "FIX_VALID_07",
        "path": "fixtures/L4_rule_chain_dsl/valid/fix_valid_07.json",
        "expect": {}
      },
      {
        "id": "FIX_VALID_08",
        "path": "fixtures/L4_rule_chain_dsl/valid/fix_valid_08.json",
        "expect": {}
      },
      {
        "id": "FIX_VALID_09",
        "path": "fixtures/L4_rule_chain_dsl/valid/fix_valid_09.json",
        "expect": {}
      },
      {
        "id": "FIX_VALID_10",
        "path": "fixtures/L4_rule_chain_dsl/valid/fix_valid_10.json",
        "expect": {}
      }
    ],
    "invalid": [
      {
        "id": "FIX_INVALID_01",
        "path": "fixtures/L4_rule_chain_dsl/invalid/fix_invalid_01.json",
        "expect": {},
        "expect_errors": [
          {
            "code": "E_LAYER_CHAIN_DSL_INVALID",
            "blame_pointers": [
              "/chains"
            ],
            "dag_node_ids": [
              "rule_chain_dsl_node_01"
            ]
          }
        ]
      },
      {
        "id": "FIX_INVALID_02",
        "path": "fixtures/L4_rule_chain_dsl/invalid/fix_invalid_02.json",
        "expect": {},
        "expect_errors": [
          {
            "code": "E_LAYER_CHAIN_DSL_INVALID",
            "blame_pointers": [
              "/chains"
            ],
            "dag_node_ids": [
              "rule_chain_dsl_node_02"
            ]
          }
        ]
      },
      {
        "id": "FIX_INVALID_03",
        "path": "fixtures/L4_rule_chain_dsl/invalid/fix_invalid_03.json",
        "expect": {},
        "expect_errors": [
          {
            "code": "E_LAYER_CHAIN_DSL_INVALID",
            "blame_pointers": [
              "/chains"
            ],
            "dag_node_ids": [
              "rule_chain_dsl_node_03"
            ]
          }
        ]
      },
      {
        "id": "FIX_INVALID_04",
        "path": "fixtures/L4_rule_chain_dsl/invalid/fix_invalid_04.json",
        "expect": {},
        "expect_errors": [
          {
            "code": "E_LAYER_CHAIN_DSL_INVALID",
            "blame_pointers": [
              "/chains"
            ],
            "dag_node_ids": [
              "rule_chain_dsl_node_04"
            ]
          }
        ]
      },
      {
        "id": "FIX_INVALID_05",
        "path": "fixtures/L4_rule_chain_dsl/invalid/fix_invalid_05.json",
        "expect": {},
        "expect_errors": [
          {
            "code": "E_LAYER_CHAIN_DSL_INVALID",
            "blame_pointers": [
              "/chains"
            ],
            "dag_node_ids": [
              "rule_chain_dsl_node_05"
            ]
          }
        ]
      },
      {
        "id": "FIX_INVALID_06",
        "path": "fixtures/L4_rule_chain_dsl/invalid/fix_invalid_06.json",
        "expect": {},
        "expect_errors": [
          {
            "code": "E_LAYER_CHAIN_DSL_INVALID",
            "blame_pointers": [
              "/chains"
            ],
            "dag_node_ids": [
              "rule_chain_dsl_node_06"
            ]
          }
        ]
      },
      {
        "id": "FIX_INVALID_07",
        "path": "fixtures/L4_rule_chain_dsl/invalid/fix_invalid_07.json",
        "expect": {},
        "expect_errors": [
          {
            "code": "E_LAYER_CHAIN_DSL_INVALID",
            "blame_pointers": [
              "/chains"
            ],
            "dag_node_ids": [
              "rule_chain_dsl_node_07"
            ]
          }
        ]
      },
      {
        "id": "FIX_INVALID_08",
        "path": "fixtures/L4_rule_chain_dsl/invalid/fix_invalid_08.json",
        "expect": {},
        "expect_errors": [
          {
            "code": "E_LAYER_CHAIN_DSL_INVALID",
            "blame_pointers": [
              "/chains"
            ],
            "dag_node_ids": [
              "rule_chain_dsl_node_08"
            ]
          }
        ]
      },
      {
        "id": "FIX_INVALID_09",
        "path": "fixtures/L4_rule_chain_dsl/invalid/fix_invalid_09.json",
        "expect": {},
        "expect_errors": [
          {
            "code": "E_LAYER_CHAIN_DSL_INVALID",
            "blame_pointers": [
              "/chains"
            ],
            "dag_node_ids": [
              "rule_chain_dsl_node_09"
            ]
          }
        ]
      },
      {
        "id": "FIX_INVALID_10",
        "path": "fixtures/L4_rule_chain_dsl/invalid/fix_invalid_10.json",
        "expect": {},
        "expect_errors": [
          {
            "code": "E_LAYER_CHAIN_DSL_INVALID",
            "blame_pointers": [
              "/chains"
            ],
            "dag_node_ids": [
              "rule_chain_dsl_node_10"
            ]
          }
        ]
      }
    ],
    "minimums": {
      "valid": 10,
      "invalid": 10
    }
  },
  "capabilities": {
    "introduces": [
      {
        "id": "cap:sre:rule_chain_dsl:v1",
        "version": 1,
        "summary": "Chains, gates and signals DSL compilation surface."
      }
    ],
    "requires": [],
    "optional": [],
    "deprecates": []
  },
  "hashes": {
    "doc_sha256": "sha256:bcc7d3726cfb383e1d79af906d5bfde06e8f1412429d9abde813f9a90f43fddb",
    "contract_sha256": "sha256:bcc7d3726cfb383e1d79af906d5bfde06e8f1412429d9abde813f9a90f43fddb"
  }
}
```
