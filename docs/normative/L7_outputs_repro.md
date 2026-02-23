# L7_outputs_repro Normative Specification

Layer: `outputs_repro`

This normative document defines the machine-checkable contract for `outputs_repro`.

## Contract Block

```json
{
  "contract_block_version": 1,
  "contract_semantics_version": 1,
  "layer_id": "outputs_repro",
  "layer_version": 1,
  "schema_scope": {
    "owned_pointers": [
      "/outputs",
      "/execution/permissions"
    ],
    "owned_defs": [],
    "notes": "Dataset and reproducible artifact emission."
  },
  "dependencies": {
    "allowed_layer_ids": [
      "core_identity_io",
      "universe_data",
      "sierra_runtime_topology",
      "studies_features",
      "rule_chain_dsl",
      "experimentation_permute",
      "semantics_integrity"
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
      },
      {
        "interface_id": "artifact_io",
        "version": 1
      }
    ]
  },
  "ast_nodes": [
    {
      "node_id": "OutputsReproNode",
      "kind": "struct",
      "fields": [
        {
          "name": "layer_id",
          "type": "string",
          "required": true,
          "default": "outputs_repro"
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
                "/outputs",
                "/execution/permissions"
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
        "name": "ValidateOutputsRepro",
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
        "name": "RegisterOutputsReproApi",
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
        "code": "E_IO_OUTPUT_CONFIG_INVALID",
        "layer_id": "outputs_repro",
        "severity": "error",
        "message_template": "outputs_repro contract violation: {reason}"
      },
      {
        "code": "E_LAYER_AST_CHECK_FAILED",
        "layer_id": "outputs_repro",
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
                "function": "ValidateOutputsRepro",
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
                "path": "docs/normative/L7_outputs_repro.md"
              }
            }
          },
          {
            "id": "CHK_FILE_CONTRACT_MANIFEST_EXISTS",
            "severity": "error",
            "assert": {
              "kind": "file_exists",
              "params": {
                "path": "contracts/L7_outputs_repro.manifest.json"
              }
            }
          },
          {
            "id": "CHK_FILE_FORBIDDEN_TEMP",
            "severity": "warn",
            "assert": {
              "kind": "file_forbidden",
              "params": {
                "path": "src/layers/L7_outputs_repro/tmp.txt"
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
                "layer_id": "outputs_repro",
                "allowed_layer_ids": [
                  "core_identity_io",
                  "universe_data",
                  "sierra_runtime_topology",
                  "studies_features",
                  "rule_chain_dsl",
                  "experimentation_permute",
                  "semantics_integrity"
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
                "node_id": "OutputsReproNode"
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
                  "/outputs",
                  "/execution/permissions"
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
                "expected_code": "E_IO_OUTPUT_CONFIG_INVALID",
                "expected_blame_pointer": "/outputs"
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
                "expected_code": "E_IO_OUTPUT_CONFIG_INVALID",
                "expected_blame_pointer": "/outputs"
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
                "expected_code": "E_IO_OUTPUT_CONFIG_INVALID",
                "expected_blame_pointer": "/outputs"
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
                "expected_code": "E_IO_OUTPUT_CONFIG_INVALID",
                "expected_blame_pointer": "/outputs"
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
                "expected_code": "E_IO_OUTPUT_CONFIG_INVALID",
                "expected_blame_pointer": "/outputs"
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
                "expected_code": "E_IO_OUTPUT_CONFIG_INVALID",
                "expected_blame_pointer": "/outputs"
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
                "expected_code": "E_IO_OUTPUT_CONFIG_INVALID",
                "expected_blame_pointer": "/outputs"
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
                "expected_code": "E_IO_OUTPUT_CONFIG_INVALID",
                "expected_blame_pointer": "/outputs"
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
                "expected_code": "E_IO_OUTPUT_CONFIG_INVALID",
                "expected_blame_pointer": "/outputs"
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
                "expected_code": "E_IO_OUTPUT_CONFIG_INVALID",
                "expected_blame_pointer": "/outputs"
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
        "path": "generated/contracts/L7_outputs_repro_api.h",
        "kind": "header"
      },
      {
        "id": "ART_LAYER_CONTRACT",
        "path": "contracts/L7_outputs_repro.manifest.json",
        "kind": "manifest"
      },
      {
        "id": "ART_LAYER_TEST_STRUCTURAL",
        "path": "generated/tests/L7_outputs_repro_structural.json",
        "kind": "tests"
      },
      {
        "id": "ART_LAYER_TEST_SEMANTIC",
        "path": "generated/tests/L7_outputs_repro_semantic.json",
        "kind": "tests"
      },
      {
        "id": "ART_LAYER_DOC",
        "path": "docs/normative/L7_outputs_repro.md",
        "kind": "doc"
      }
    ]
  },
  "fixtures": {
    "valid": [
      {
        "id": "FIX_VALID_01",
        "path": "fixtures/L7_outputs_repro/valid/fix_valid_01.json",
        "expect": {}
      },
      {
        "id": "FIX_VALID_02",
        "path": "fixtures/L7_outputs_repro/valid/fix_valid_02.json",
        "expect": {}
      },
      {
        "id": "FIX_VALID_03",
        "path": "fixtures/L7_outputs_repro/valid/fix_valid_03.json",
        "expect": {}
      },
      {
        "id": "FIX_VALID_04",
        "path": "fixtures/L7_outputs_repro/valid/fix_valid_04.json",
        "expect": {}
      },
      {
        "id": "FIX_VALID_05",
        "path": "fixtures/L7_outputs_repro/valid/fix_valid_05.json",
        "expect": {}
      },
      {
        "id": "FIX_VALID_06",
        "path": "fixtures/L7_outputs_repro/valid/fix_valid_06.json",
        "expect": {}
      },
      {
        "id": "FIX_VALID_07",
        "path": "fixtures/L7_outputs_repro/valid/fix_valid_07.json",
        "expect": {}
      },
      {
        "id": "FIX_VALID_08",
        "path": "fixtures/L7_outputs_repro/valid/fix_valid_08.json",
        "expect": {}
      },
      {
        "id": "FIX_VALID_09",
        "path": "fixtures/L7_outputs_repro/valid/fix_valid_09.json",
        "expect": {}
      },
      {
        "id": "FIX_VALID_10",
        "path": "fixtures/L7_outputs_repro/valid/fix_valid_10.json",
        "expect": {}
      }
    ],
    "invalid": [
      {
        "id": "FIX_INVALID_01",
        "path": "fixtures/L7_outputs_repro/invalid/fix_invalid_01.json",
        "expect": {},
        "expect_errors": [
          {
            "code": "E_IO_OUTPUT_CONFIG_INVALID",
            "blame_pointers": [
              "/outputs"
            ],
            "dag_node_ids": [
              "outputs_repro_node_01"
            ]
          }
        ]
      },
      {
        "id": "FIX_INVALID_02",
        "path": "fixtures/L7_outputs_repro/invalid/fix_invalid_02.json",
        "expect": {},
        "expect_errors": [
          {
            "code": "E_IO_OUTPUT_CONFIG_INVALID",
            "blame_pointers": [
              "/outputs"
            ],
            "dag_node_ids": [
              "outputs_repro_node_02"
            ]
          }
        ]
      },
      {
        "id": "FIX_INVALID_03",
        "path": "fixtures/L7_outputs_repro/invalid/fix_invalid_03.json",
        "expect": {},
        "expect_errors": [
          {
            "code": "E_IO_OUTPUT_CONFIG_INVALID",
            "blame_pointers": [
              "/outputs"
            ],
            "dag_node_ids": [
              "outputs_repro_node_03"
            ]
          }
        ]
      },
      {
        "id": "FIX_INVALID_04",
        "path": "fixtures/L7_outputs_repro/invalid/fix_invalid_04.json",
        "expect": {},
        "expect_errors": [
          {
            "code": "E_IO_OUTPUT_CONFIG_INVALID",
            "blame_pointers": [
              "/outputs"
            ],
            "dag_node_ids": [
              "outputs_repro_node_04"
            ]
          }
        ]
      },
      {
        "id": "FIX_INVALID_05",
        "path": "fixtures/L7_outputs_repro/invalid/fix_invalid_05.json",
        "expect": {},
        "expect_errors": [
          {
            "code": "E_IO_OUTPUT_CONFIG_INVALID",
            "blame_pointers": [
              "/outputs"
            ],
            "dag_node_ids": [
              "outputs_repro_node_05"
            ]
          }
        ]
      },
      {
        "id": "FIX_INVALID_06",
        "path": "fixtures/L7_outputs_repro/invalid/fix_invalid_06.json",
        "expect": {},
        "expect_errors": [
          {
            "code": "E_IO_OUTPUT_CONFIG_INVALID",
            "blame_pointers": [
              "/outputs"
            ],
            "dag_node_ids": [
              "outputs_repro_node_06"
            ]
          }
        ]
      },
      {
        "id": "FIX_INVALID_07",
        "path": "fixtures/L7_outputs_repro/invalid/fix_invalid_07.json",
        "expect": {},
        "expect_errors": [
          {
            "code": "E_IO_OUTPUT_CONFIG_INVALID",
            "blame_pointers": [
              "/outputs"
            ],
            "dag_node_ids": [
              "outputs_repro_node_07"
            ]
          }
        ]
      },
      {
        "id": "FIX_INVALID_08",
        "path": "fixtures/L7_outputs_repro/invalid/fix_invalid_08.json",
        "expect": {},
        "expect_errors": [
          {
            "code": "E_IO_OUTPUT_CONFIG_INVALID",
            "blame_pointers": [
              "/outputs"
            ],
            "dag_node_ids": [
              "outputs_repro_node_08"
            ]
          }
        ]
      },
      {
        "id": "FIX_INVALID_09",
        "path": "fixtures/L7_outputs_repro/invalid/fix_invalid_09.json",
        "expect": {},
        "expect_errors": [
          {
            "code": "E_IO_OUTPUT_CONFIG_INVALID",
            "blame_pointers": [
              "/outputs"
            ],
            "dag_node_ids": [
              "outputs_repro_node_09"
            ]
          }
        ]
      },
      {
        "id": "FIX_INVALID_10",
        "path": "fixtures/L7_outputs_repro/invalid/fix_invalid_10.json",
        "expect": {},
        "expect_errors": [
          {
            "code": "E_IO_OUTPUT_CONFIG_INVALID",
            "blame_pointers": [
              "/outputs"
            ],
            "dag_node_ids": [
              "outputs_repro_node_10"
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
        "id": "cap:sre:outputs_repro:v1",
        "version": 1,
        "summary": "Dataset and reproducible artifact emission."
      }
    ],
    "requires": [],
    "optional": [],
    "deprecates": []
  },
  "hashes": {
    "doc_sha256": "sha256:723b050d0838a9787d1435bc603a5b90e211cc3f6fea4e8d766db8ce63bdd6c2",
    "contract_sha256": "sha256:0b5c6340c381eca37535e838b2640b1462163921ff7459228b9083b6bc5cb416"
  }
}
```
