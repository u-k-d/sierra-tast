# L6_semantics_integrity Normative Specification

Layer: `semantics_integrity`

This normative document defines the machine-checkable contract for `semantics_integrity`.

## Contract Block

```json
{
  "contract_block_version": 1,
  "contract_semantics_version": 1,
  "layer_id": "semantics_integrity",
  "layer_version": 1,
  "schema_scope": {
    "owned_pointers": [
      "/validation",
      "/execution",
      "/studies",
      "/chains",
      "/parameters",
      "/gates",
      "/outputs"
    ],
    "owned_defs": [],
    "notes": "Cross-layer semantic integrity and tripwires."
  },
  "dependencies": {
    "allowed_layer_ids": [
      "core_identity_io",
      "universe_data",
      "sierra_runtime_topology",
      "studies_features",
      "rule_chain_dsl",
      "experimentation_permute"
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
      "node_id": "SemanticsIntegrityNode",
      "kind": "struct",
      "fields": [
        {
          "name": "layer_id",
          "type": "string",
          "required": true,
          "default": "semantics_integrity"
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
                "/validation",
                "/execution",
                "/studies",
                "/chains",
                "/parameters",
                "/gates",
                "/outputs"
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
        "name": "ValidateSemanticsIntegrity",
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
        "name": "RegisterSemanticsIntegrityApi",
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
        "code": "E_SEM_INTEGRITY_RULE_FAILED",
        "layer_id": "semantics_integrity",
        "severity": "error",
        "message_template": "semantics_integrity contract violation: {reason}"
      },
      {
        "code": "E_LAYER_AST_CHECK_FAILED",
        "layer_id": "semantics_integrity",
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
                "function": "ValidateSemanticsIntegrity",
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
                "path": "docs/normative/L6_semantics_integrity.md"
              }
            }
          },
          {
            "id": "CHK_FILE_CONTRACT_MANIFEST_EXISTS",
            "severity": "error",
            "assert": {
              "kind": "file_exists",
              "params": {
                "path": "contracts/L6_semantics_integrity.manifest.json"
              }
            }
          },
          {
            "id": "CHK_FILE_FORBIDDEN_TEMP",
            "severity": "warn",
            "assert": {
              "kind": "file_forbidden",
              "params": {
                "path": "src/layers/L6_semantics_integrity/tmp.txt"
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
                "layer_id": "semantics_integrity",
                "allowed_layer_ids": [
                  "core_identity_io",
                  "universe_data",
                  "sierra_runtime_topology",
                  "studies_features",
                  "rule_chain_dsl",
                  "experimentation_permute"
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
                "node_id": "SemanticsIntegrityNode"
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
                  "/validation",
                  "/execution",
                  "/studies",
                  "/chains",
                  "/parameters",
                  "/gates",
                  "/outputs"
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
                "expected_code": "E_SEM_INTEGRITY_RULE_FAILED",
                "expected_blame_pointer": "/validation"
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
                "expected_code": "E_SEM_INTEGRITY_RULE_FAILED",
                "expected_blame_pointer": "/validation"
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
                "expected_code": "E_SEM_INTEGRITY_RULE_FAILED",
                "expected_blame_pointer": "/validation"
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
                "expected_code": "E_SEM_INTEGRITY_RULE_FAILED",
                "expected_blame_pointer": "/validation"
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
                "expected_code": "E_SEM_INTEGRITY_RULE_FAILED",
                "expected_blame_pointer": "/validation"
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
                "expected_code": "E_SEM_INTEGRITY_RULE_FAILED",
                "expected_blame_pointer": "/validation"
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
                "expected_code": "E_SEM_INTEGRITY_RULE_FAILED",
                "expected_blame_pointer": "/validation"
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
                "expected_code": "E_SEM_INTEGRITY_RULE_FAILED",
                "expected_blame_pointer": "/validation"
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
                "expected_code": "E_SEM_INTEGRITY_RULE_FAILED",
                "expected_blame_pointer": "/validation"
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
                "expected_code": "E_SEM_INTEGRITY_RULE_FAILED",
                "expected_blame_pointer": "/validation"
              }
            }
          },
          {
            "id": "CHK_DIAG_INVALID_11",
            "severity": "error",
            "assert": {
              "kind": "invalid_fixture_yields_error",
              "params": {
                "fixture_id": "FIX_INVALID_11",
                "expected_code": "E_SEM_INTEGRITY_RULE_FAILED",
                "expected_blame_pointer": "/validation"
              }
            }
          },
          {
            "id": "CHK_DIAG_INVALID_12",
            "severity": "error",
            "assert": {
              "kind": "invalid_fixture_yields_error",
              "params": {
                "fixture_id": "FIX_INVALID_12",
                "expected_code": "E_SEM_INTEGRITY_RULE_FAILED",
                "expected_blame_pointer": "/validation"
              }
            }
          },
          {
            "id": "CHK_DIAG_INVALID_13",
            "severity": "error",
            "assert": {
              "kind": "invalid_fixture_yields_error",
              "params": {
                "fixture_id": "FIX_INVALID_13",
                "expected_code": "E_SEM_INTEGRITY_RULE_FAILED",
                "expected_blame_pointer": "/validation"
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
        "path": "generated/contracts/L6_semantics_integrity_api.h",
        "kind": "header"
      },
      {
        "id": "ART_LAYER_CONTRACT",
        "path": "contracts/L6_semantics_integrity.manifest.json",
        "kind": "manifest"
      },
      {
        "id": "ART_LAYER_TEST_STRUCTURAL",
        "path": "generated/tests/L6_semantics_integrity_structural.json",
        "kind": "tests"
      },
      {
        "id": "ART_LAYER_TEST_SEMANTIC",
        "path": "generated/tests/L6_semantics_integrity_semantic.json",
        "kind": "tests"
      },
      {
        "id": "ART_LAYER_DOC",
        "path": "docs/normative/L6_semantics_integrity.md",
        "kind": "doc"
      }
    ]
  },
  "fixtures": {
    "valid": [
      {
        "id": "FIX_VALID_01",
        "path": "fixtures/L6_semantics_integrity/valid/fix_valid_01.json",
        "expect": {}
      },
      {
        "id": "FIX_VALID_02",
        "path": "fixtures/L6_semantics_integrity/valid/fix_valid_02.json",
        "expect": {}
      },
      {
        "id": "FIX_VALID_03",
        "path": "fixtures/L6_semantics_integrity/valid/fix_valid_03.json",
        "expect": {}
      },
      {
        "id": "FIX_VALID_04",
        "path": "fixtures/L6_semantics_integrity/valid/fix_valid_04.json",
        "expect": {}
      },
      {
        "id": "FIX_VALID_05",
        "path": "fixtures/L6_semantics_integrity/valid/fix_valid_05.json",
        "expect": {}
      },
      {
        "id": "FIX_VALID_06",
        "path": "fixtures/L6_semantics_integrity/valid/fix_valid_06.json",
        "expect": {}
      },
      {
        "id": "FIX_VALID_07",
        "path": "fixtures/L6_semantics_integrity/valid/fix_valid_07.json",
        "expect": {}
      },
      {
        "id": "FIX_VALID_08",
        "path": "fixtures/L6_semantics_integrity/valid/fix_valid_08.json",
        "expect": {}
      },
      {
        "id": "FIX_VALID_09",
        "path": "fixtures/L6_semantics_integrity/valid/fix_valid_09.json",
        "expect": {}
      },
      {
        "id": "FIX_VALID_10",
        "path": "fixtures/L6_semantics_integrity/valid/fix_valid_10.json",
        "expect": {}
      }
    ],
    "invalid": [
      {
        "id": "FIX_INVALID_01",
        "path": "fixtures/L6_semantics_integrity/invalid/fix_invalid_01.json",
        "expect": {},
        "expect_errors": [
          {
            "code": "E_SEM_INTEGRITY_RULE_FAILED",
            "blame_pointers": [
              "/validation"
            ],
            "dag_node_ids": [
              "semantics_integrity_node_01"
            ]
          }
        ]
      },
      {
        "id": "FIX_INVALID_02",
        "path": "fixtures/L6_semantics_integrity/invalid/fix_invalid_02.json",
        "expect": {},
        "expect_errors": [
          {
            "code": "E_SEM_INTEGRITY_RULE_FAILED",
            "blame_pointers": [
              "/validation"
            ],
            "dag_node_ids": [
              "semantics_integrity_node_02"
            ]
          }
        ]
      },
      {
        "id": "FIX_INVALID_03",
        "path": "fixtures/L6_semantics_integrity/invalid/fix_invalid_03.json",
        "expect": {},
        "expect_errors": [
          {
            "code": "E_SEM_INTEGRITY_RULE_FAILED",
            "blame_pointers": [
              "/validation"
            ],
            "dag_node_ids": [
              "semantics_integrity_node_03"
            ]
          }
        ]
      },
      {
        "id": "FIX_INVALID_04",
        "path": "fixtures/L6_semantics_integrity/invalid/fix_invalid_04.json",
        "expect": {},
        "expect_errors": [
          {
            "code": "E_SEM_INTEGRITY_RULE_FAILED",
            "blame_pointers": [
              "/validation"
            ],
            "dag_node_ids": [
              "semantics_integrity_node_04"
            ]
          }
        ]
      },
      {
        "id": "FIX_INVALID_05",
        "path": "fixtures/L6_semantics_integrity/invalid/fix_invalid_05.json",
        "expect": {},
        "expect_errors": [
          {
            "code": "E_SEM_INTEGRITY_RULE_FAILED",
            "blame_pointers": [
              "/validation"
            ],
            "dag_node_ids": [
              "semantics_integrity_node_05"
            ]
          }
        ]
      },
      {
        "id": "FIX_INVALID_06",
        "path": "fixtures/L6_semantics_integrity/invalid/fix_invalid_06.json",
        "expect": {},
        "expect_errors": [
          {
            "code": "E_SEM_INTEGRITY_RULE_FAILED",
            "blame_pointers": [
              "/validation"
            ],
            "dag_node_ids": [
              "semantics_integrity_node_06"
            ]
          }
        ]
      },
      {
        "id": "FIX_INVALID_07",
        "path": "fixtures/L6_semantics_integrity/invalid/fix_invalid_07.json",
        "expect": {},
        "expect_errors": [
          {
            "code": "E_SEM_INTEGRITY_RULE_FAILED",
            "blame_pointers": [
              "/validation"
            ],
            "dag_node_ids": [
              "semantics_integrity_node_07"
            ]
          }
        ]
      },
      {
        "id": "FIX_INVALID_08",
        "path": "fixtures/L6_semantics_integrity/invalid/fix_invalid_08.json",
        "expect": {},
        "expect_errors": [
          {
            "code": "E_SEM_INTEGRITY_RULE_FAILED",
            "blame_pointers": [
              "/validation"
            ],
            "dag_node_ids": [
              "semantics_integrity_node_08"
            ]
          }
        ]
      },
      {
        "id": "FIX_INVALID_09",
        "path": "fixtures/L6_semantics_integrity/invalid/fix_invalid_09.json",
        "expect": {},
        "expect_errors": [
          {
            "code": "E_SEM_INTEGRITY_RULE_FAILED",
            "blame_pointers": [
              "/validation"
            ],
            "dag_node_ids": [
              "semantics_integrity_node_09"
            ]
          }
        ]
      },
      {
        "id": "FIX_INVALID_10",
        "path": "fixtures/L6_semantics_integrity/invalid/fix_invalid_10.json",
        "expect": {},
        "expect_errors": [
          {
            "code": "E_SEM_INTEGRITY_RULE_FAILED",
            "blame_pointers": [
              "/validation"
            ],
            "dag_node_ids": [
              "semantics_integrity_node_10"
            ]
          }
        ]
      },
      {
        "id": "FIX_INVALID_11",
        "path": "fixtures/L6_semantics_integrity/invalid/fix_invalid_11.json",
        "expect": {},
        "expect_errors": [
          {
            "code": "E_SEM_INTEGRITY_RULE_FAILED",
            "blame_pointers": [
              "/validation"
            ],
            "dag_node_ids": [
              "semantics_integrity_node_11"
            ]
          }
        ]
      },
      {
        "id": "FIX_INVALID_12",
        "path": "fixtures/L6_semantics_integrity/invalid/fix_invalid_12.json",
        "expect": {},
        "expect_errors": [
          {
            "code": "E_SEM_INTEGRITY_RULE_FAILED",
            "blame_pointers": [
              "/validation"
            ],
            "dag_node_ids": [
              "semantics_integrity_node_12"
            ]
          }
        ]
      },
      {
        "id": "FIX_INVALID_13",
        "path": "fixtures/L6_semantics_integrity/invalid/fix_invalid_13.json",
        "expect": {},
        "expect_errors": [
          {
            "code": "E_SEM_INTEGRITY_RULE_FAILED",
            "blame_pointers": [
              "/validation"
            ],
            "dag_node_ids": [
              "semantics_integrity_node_13"
            ]
          }
        ]
      }
    ],
    "minimums": {
      "valid": 10,
      "invalid": 13
    }
  },
  "capabilities": {
    "introduces": [
      {
        "id": "cap:sre:semantics_integrity:v1",
        "version": 1,
        "summary": "Cross-layer semantic integrity and tripwires."
      }
    ],
    "requires": [],
    "optional": [],
    "deprecates": []
  },
  "hashes": {
    "doc_sha256": "sha256:bd4754df554e8116069a36d57bce59352f25c3e555066df8f6f67fa0bbbe6219",
    "contract_sha256": "sha256:bd4754df554e8116069a36d57bce59352f25c3e555066df8f6f67fa0bbbe6219"
  }
}
```
