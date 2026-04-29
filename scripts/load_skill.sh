#!/usr/bin/env bash
# load_skill.sh — list or display AICamRobot skills

SKILLS_DIR="$(cd "$(dirname "$0")/../skills" && pwd)"

usage() {
    echo "Usage:"
    echo "  $0 list               List all available skills"
    echo "  $0 show <skill-id>    Print the SKILL.md for a skill"
    echo "  $0 search <keyword>   Search skill titles and descriptions"
}

list_skills() {
    echo ""
    echo "AICamRobot Skills"
    echo "================="
    # Parse manifest.json with awk (no jq dependency)
    awk '
        /"id":/      { gsub(/.*"id": *"/, ""); gsub(/".*/, ""); id=$0 }
        /"title":/   { gsub(/.*"title": *"/, ""); gsub(/".*/, ""); title=$0 }
        /"description":/ {
            gsub(/.*"description": *"/, ""); gsub(/".*/, ""); desc=$0
            printf "  %-22s %s\n", id, desc
        }
    ' "$SKILLS_DIR/manifest.json"
    echo ""
    echo "Run:  $0 show <skill-id>   to read a skill"
}

show_skill() {
    local id="$1"
    local skill_file="$SKILLS_DIR/$id/SKILL.md"
    if [ ! -f "$skill_file" ]; then
        echo "Error: skill '$id' not found."
        echo "Run '$0 list' to see available skills."
        exit 1
    fi
    cat "$skill_file"
}

search_skills() {
    local keyword="$1"
    echo ""
    echo "Skills matching '$keyword':"
    echo "---------------------------"
    grep -ri "$keyword" "$SKILLS_DIR"/*/SKILL.md \
        | sed "s|$SKILLS_DIR/||; s|/SKILL.md:| → |" \
        | head -40
}

case "${1:-}" in
    list)
        list_skills
        ;;
    show)
        if [ -z "${2:-}" ]; then
            echo "Error: provide a skill id."
            usage
            exit 1
        fi
        show_skill "$2"
        ;;
    search)
        if [ -z "${2:-}" ]; then
            echo "Error: provide a search keyword."
            usage
            exit 1
        fi
        search_skills "$2"
        ;;
    *)
        usage
        echo ""
        list_skills
        ;;
esac
