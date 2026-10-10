#!/usr/bin/make -f
# scan.mk - static analysis + SonarQube (sonar-cxx) for C/C++ projects

REPORT_DIR ?= report
SRC_DIRS ?= src
SCAN_INCLUDES ?= -I include -I src -Isrc/includes
SCAN_STD ?= c99
SONAR_SCANNER ?= sonar-scanner

ifneq (,$(wildcard ./.env))
include .env
export
endif

.PHONY: scan-cppcheck scan-clang-tidy scan-coverage scan-sonar sonarqube scan-clean scan-help

scan-cppcheck:
	@mkdir -p $(REPORT_DIR)
	-cppcheck --enable=all --xml --xml-version=2 \
		--suppress=missingIncludeSystem \
		--language=c --std=$(SCAN_STD) \
		$(SCAN_INCLUDES) \
		$(SRC_DIRS) 2> $(REPORT_DIR)/cppcheck-report.xml

scan-clang-tidy:
	@mkdir -p $(REPORT_DIR)
	-find $(SRC_DIRS) \( -name '*.c' -o -name '*.cpp' \) -print \
		| xargs -r clang-tidy -- $(SCAN_INCLUDES) \
		> $(REPORT_DIR)/clang-tidy-report.txt 2>&1 || true

scan-coverage:
	@mkdir -p $(REPORT_DIR)
	-@$(MAKE) coverage
	-@test -f coverage.xml && cp -f coverage.xml $(REPORT_DIR)/coverage.xml || true
	-@test -f build/coverage.xml && cp -f build/coverage.xml $(REPORT_DIR)/coverage.xml || true

scan-sonar: scan-cppcheck
	$(SONAR_SCANNER) \
		-Dsonar.token=$(SONAR_TOKEN) \
		-Dsonar.host.url=$(SONAR_HOST_URL) \
		-Dsonar.projectKey=$(SONAR_PROJECT_KEY)

sonarqube: scan-sonar

scan-clean:
	rm -rf $(REPORT_DIR)

scan-help:
	@echo "scan-cppcheck scan-clang-tidy scan-coverage scan-sonar sonarqube scan-clean"
