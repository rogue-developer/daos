//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// +build linux,amd64
//

package telemetry

import (
	"context"
	"testing"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/pkg/errors"
)

func TestTelemetry_GetCounter(t *testing.T) {
	testCtx, testMetrics := setupTestMetrics(t)
	defer cleanupTestMetricsProducer(t)

	realCounter, ok := testMetrics[MetricTypeCounter]
	if !ok {
		t.Fatal("real counter not in metrics set")
	}
	counterName := realCounter.name

	for name, tc := range map[string]struct {
		ctx        context.Context
		metricName string
		expResult  *testMetric
		expErr     error
	}{
		"nil ctx": {
			metricName: counterName,
			expErr:     errors.New("nil context"),
		},
		"non-handle ctx": {
			ctx:        context.TODO(),
			metricName: counterName,
			expErr:     errors.New("no handle"),
		},
		"bad name": {
			ctx:        testCtx,
			metricName: "not_a_real_metric",
			expErr:     errors.New("unable to find metric"),
		},
		"bad type": {
			ctx:        testCtx,
			metricName: testMetrics[MetricTypeGauge].name,
			expErr:     errors.New("not a counter"),
		},
		"success": {
			ctx:        testCtx,
			metricName: counterName,
			expResult:  realCounter,
		},
	} {
		t.Run(name, func(t *testing.T) {
			result, err := GetCounter(tc.ctx, tc.metricName)

			common.CmpErr(t, tc.expErr, err)

			if tc.expResult != nil {
				if result == nil {
					t.Fatalf("expected non-nil result matching %+v", tc.expResult)
				}

				common.AssertEqual(t, result.Type(), MetricTypeCounter, "bad type")
				common.AssertEqual(t, result.Name(), tc.expResult.name, "bad name")
				common.AssertEqual(t, result.Path(), tc.expResult.name, "bad path")
				common.AssertEqual(t, result.Desc(), tc.expResult.desc, "bad desc")
				common.AssertEqual(t, result.Units(), tc.expResult.units, "bad units")
				common.AssertEqual(t, result.Value(), uint64(tc.expResult.cur), "bad value")
				common.AssertEqual(t, result.FloatValue(), tc.expResult.cur, "bad float value")
			} else {
				if result != nil {
					t.Fatalf("expected nil result, got %+v", result)
				}
			}
		})
	}
}
