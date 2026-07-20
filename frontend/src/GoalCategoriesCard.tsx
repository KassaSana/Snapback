import { useGoalCategories } from "./useGoalCategories";

export function GoalCategoriesCard() {
  const { categories, save, status, update } = useGoalCategories();

  return (
    <section className="card config-card">
      <div className="card-header">
        <h2>Goal Categories</h2>
        <span className="pill">editable keywords</span>
      </div>
      <p className="helper-text">Keywords connect session goals to the work contexts that matter to you.</p>
      {categories.map((category, index) => (
        <div className="category-editor" key={`${category.name}-${index}`}>
          <label className="field">
            <span>Category</span>
            <input value={category.name} onChange={(event) => update(index, "name", event.target.value)} />
          </label>
          <label className="field">
            <span>Keywords</span>
            <input value={category.keywords.join(", ")} onChange={(event) => update(index, "keywords", event.target.value)} />
          </label>
        </div>
      ))}
      <button className="secondary-button" disabled={categories.length === 0} onClick={() => void save()}>
        Save categories
      </button>
      {status ? <p className="helper-text">{status}</p> : null}
    </section>
  );
}
